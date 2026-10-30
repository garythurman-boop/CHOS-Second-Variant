#include "chos/storage.h"
#include "chos/virtio_blk.h"
#include "chos/romfs.h"
#include "chos/spinlock.h"
#include "chos/gpt.h"
#include "chos/syscall.h"
#include "chos/file_write_api.h"

static chos_block_device_t boot_disk; /* whole physical device */
static chos_block_device_t gpt_fs_disk; /* bounded CHOS partition view, if present */
static chos_block_slice_context_t gpt_fs_context;
static chos_gpt_partition_t gpt_partitions[CHOS_GPT_MAX_PARTITIONS];
static chos_block_device_t *filesystem_disk;
static chos_chfs_t root_fs;
static chos_block_device_t checked_fs_disk;
static chos_storage_versions_t file_versions;
/* Fixed scratch is serialized by storage_lock; never put a32KiB catalogue on
 * a16KiB kernel stack or allocate while the I/O/IRQ ownership lock is held. */
static chos_chfs_entry_t previous_entries[CHOS_CHFS_MAX_FILES];
static int operation_failed,operation_wrote,write_uncertain,recovering_mount;

static int disk_ready;
static int fs_ready;
static int format_allowed;
static chos_gpt_error_t last_gpt_result = CHOS_GPT_ERR_ARGUMENT;
static chos_spinlock_t storage_lock = CHOS_SPINLOCK_INITIALIZER;

static int checked_read(chos_block_device_t *device,uint64_t lba,uint32_t count,void *buffer){
    chos_block_device_t *parent=(chos_block_device_t *)device->context;
    int result=block_read(parent,lba,count,buffer);
    if(!result)operation_failed=1;
    return result;
}
static int checked_write(chos_block_device_t *device,uint64_t lba,uint32_t count,const void *buffer){
    /* An allocator backup-read failure must not be followed by a data write
     * that could overwrite extents whose ownership was not established. */
    if(operation_failed||(write_uncertain&&!recovering_mount))return 0;
    operation_wrote=1;
    int result=block_write((chos_block_device_t *)device->context,lba,count,buffer);
    if(!result){operation_failed=1;write_uncertain=1;}
    return result;
}
static int checked_flush(chos_block_device_t *device){
    if(operation_failed||(write_uncertain&&!recovering_mount))return 0;
    int result=block_flush((chos_block_device_t *)device->context);
    if(!result){operation_failed=1;write_uncertain=1;}
    return result;
}
static void operation_begin(void){operation_failed=operation_wrote=0;}
static void configure_checked_disk(void){
    checked_fs_disk=*filesystem_disk;
    checked_fs_disk.context=filesystem_disk;
    checked_fs_disk.read=checked_read;
    /* Missing flush cannot establish durable writes; keep read-only discovery
     * functional rather than manufacture a flush acknowledgement. */
    checked_fs_disk.write=filesystem_disk->write&&filesystem_disk->flush?checked_write:NULL;
    checked_fs_disk.flush=filesystem_disk->flush?checked_flush:NULL;
}
static int mount_checked(void){
    int needs_flush=write_uncertain;
    chos_storage_versions_invalidate(&file_versions);
    if(!filesystem_disk)return 0;
    configure_checked_disk();operation_begin();recovering_mount=1;
    int result=chfs_mount(&root_fs,&checked_fs_disk);
    /* Reading a complete catalogue does not acknowledge pending device-cache
     * writes after a failed flush. Require a real recovery flush before making
     * that selected catalogue writable again. */
    if(result&&needs_flush&&filesystem_disk->write&&filesystem_disk->flush&&!operation_failed)
        (void)checked_flush(&checked_fs_disk);
    recovering_mount=0;
    if(result){
        if(!operation_failed)write_uncertain=0;
        else write_uncertain=1;
        if(!chos_storage_versions_mount(&file_versions,root_fs.entries))write_uncertain=1;
    }
    return result;
}
static int write_ready_locked(void){
    return fs_ready&&filesystem_disk&&filesystem_disk->write&&filesystem_disk->flush&&
        !write_uncertain&&chos_storage_versions_can_mutate(&file_versions);
}
static void mutation_begin(void){
    operation_begin();
    for(size_t i=0;i<CHOS_CHFS_MAX_FILES;i++)previous_entries[i]=root_fs.entries[i];
}
static size_t catalogue_index(const char *path){
    for(size_t i=0;i<CHOS_CHFS_MAX_FILES;i++)if(root_fs.entries[i].path[0]&&romfs_path_equal(path,root_fs.entries[i].path))return i;
    return SIZE_MAX;
}
static int mutation_finish(int result,const char *changed_path){
    if(operation_failed&&operation_wrote)write_uncertain=1;
    size_t force=result&&changed_path?catalogue_index(changed_path):SIZE_MAX;
    if(!chos_storage_versions_update(&file_versions,previous_entries,root_fs.entries,force)){
        write_uncertain=1;return 0;
    }
    return result&&!operation_failed;
}

static int path_has_child(const char *directory, const char *path) {
    size_t n = 0u, i;
    if (directory == NULL || path == NULL) return 0;
    if (directory[0] == '/' && directory[1] == '\0') return path[0] == '/' && path[1] != '\0';
    while (directory[n] != '\0') ++n;
    for (i = 0u; i < n; ++i) {
        int a = directory[i], b = path[i];
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
        if (a != b) return 0;
    }
    return path[n] == '/';
}

int storage_attach_block_device(chos_block_device_t *device) {
    uint64_t flags;
    size_t partition_count = 0u, i;
    int mounted = 0;
    if (device == 0 || !device->ready || device->sector_count == 0u ||
        device->read == 0) return 0;
    flags = chos_spin_lock_irqsave(&storage_lock);
    boot_disk = *device;
    disk_ready = 1;
    fs_ready = 0;
    chos_storage_versions_invalidate(&file_versions);
    filesystem_disk = &boot_disk;
    format_allowed = 1; /* raw unpartitioned CHFS images remain supported for QEMU */
    if (mount_checked()) {
        fs_ready = 1;
        chos_spin_unlock_irqrestore(&storage_lock, flags);
        return 1;
    }

    /* Never format a valid GPT drive as if it were a raw CHFS volume. Look only
     * for CHOS's own partition type; unrelated partitions are left untouched. */
    last_gpt_result = chos_gpt_read(&boot_disk, gpt_partitions,
                                    CHOS_GPT_MAX_PARTITIONS, &partition_count);
    if (last_gpt_result == CHOS_GPT_OK) {
        filesystem_disk = NULL;
        format_allowed = 0;
        for (i = 0u; i < partition_count; ++i) {
            const chos_gpt_partition_t *part = &gpt_partitions[i];
            uint64_t part_sectors;
            if (!chos_gpt_guid_equal(part->type_guid, chos_gpt_chos_data_type_guid)) continue;
            part_sectors = part->last_lba - part->first_lba + 1u;
            if (!chos_block_slice_init(&gpt_fs_disk, &gpt_fs_context, &boot_disk,
                                       part->first_lba, part_sectors, "chos-gpt-data")) continue;
            filesystem_disk = &gpt_fs_disk;
            format_allowed = 1; /* explicit shell mkfs may format this CHOS partition only */
            if (mount_checked()) { fs_ready = 1; mounted = 1; break; }
        }
        (void)mounted;
    } else {
        /* Non-GPT media may still be a deliberately selected blank raw CHFS disk. */
        filesystem_disk = &boot_disk;
        format_allowed = 1;
    }
    chos_spin_unlock_irqrestore(&storage_lock, flags);
    return 1;
}

int storage_init(void) {
    chos_block_device_t device;
    disk_ready = 0;
    fs_ready = 0;
    if (!virtio_blk_init(&device)) return 0;
    return storage_attach_block_device(&device);
}
const char *storage_error(void) {
    if(write_uncertain)return "filesystem I/O uncertain; writes require a verified remount";
    if(fs_ready&&!chos_storage_versions_can_mutate(&file_versions))return "filesystem identity budget exhausted; writes unavailable";
    return virtio_blk_last_error();
}
int storage_block_ready(void) { return disk_ready; }
int storage_mounted(void) { return fs_ready; }
int storage_format(void) {
    uint64_t flags = chos_spin_lock_irqsave(&storage_lock);
    chos_storage_versions_invalidate(&file_versions);
    int result=0;
    if(disk_ready&&format_allowed&&filesystem_disk&&filesystem_disk->write&&filesystem_disk->flush){
        configure_checked_disk();operation_begin();recovering_mount=1;
        result=chfs_format(&checked_fs_disk);recovering_mount=0;
    }
    fs_ready=result?mount_checked():0;
    chos_spin_unlock_irqrestore(&storage_lock, flags);
    return fs_ready;
}
int storage_mount(void) {
    uint64_t flags = chos_spin_lock_irqsave(&storage_lock);
    fs_ready = disk_ready && filesystem_disk != NULL && mount_checked();
    chos_spin_unlock_irqrestore(&storage_lock, flags);
    return fs_ready;
}
int storage_write_ready(void){
    uint64_t flags=chos_spin_lock_irqsave(&storage_lock);
    int result=write_ready_locked();chos_spin_unlock_irqrestore(&storage_lock,flags);return result;
}

chos_block_device_t *storage_device(void) { return disk_ready ? &boot_disk : 0; }
chos_chfs_t *storage_filesystem(void) { return fs_ready ? &root_fs : 0; }
int storage_lookup(const char *path, chos_chfs_entry_t *out) {
    uint64_t flags = chos_spin_lock_irqsave(&storage_lock);
    int result = fs_ready && chfs_lookup(&root_fs, path, out);
    chos_spin_unlock_irqrestore(&storage_lock, flags);
    return result;
}
int storage_read_file(const char *path, void *buffer, size_t capacity, size_t *bytes_read) {
    uint64_t flags = chos_spin_lock_irqsave(&storage_lock);
    operation_begin();
    int result = fs_ready && chfs_read_file(&root_fs, path, buffer, capacity, bytes_read);
    chos_spin_unlock_irqrestore(&storage_lock, flags);
    return result;
}
int storage_write_file(const char *path, const void *data, size_t size) {
    uint64_t flags = chos_spin_lock_irqsave(&storage_lock);
    int result=0;
    if(write_ready_locked()){
        mutation_begin();result=mutation_finish(chfs_write_file(&root_fs, path, data, size),path);
    }
    chos_spin_unlock_irqrestore(&storage_lock, flags);
    return result;
}
int storage_remove_file(const char *path) {
    uint64_t flags = chos_spin_lock_irqsave(&storage_lock);
    int result=0;
    if(write_ready_locked()){
        mutation_begin();result=mutation_finish(chfs_remove_file(&root_fs, path),NULL);
    }
    chos_spin_unlock_irqrestore(&storage_lock, flags);
    return result;
}
int storage_rename_file(const char *old_path, const char *new_path) {
    uint64_t flags = chos_spin_lock_irqsave(&storage_lock);
    int result=0;
    if(write_ready_locked()){
        mutation_begin();result=mutation_finish(chfs_rename_file(&root_fs, old_path, new_path),new_path);
    }
    chos_spin_unlock_irqrestore(&storage_lock, flags);
    return result;
}
int storage_rename_directory(const char *old_path, const char *new_path) {
    uint64_t flags = chos_spin_lock_irqsave(&storage_lock);
    int result=0;
    if(write_ready_locked()){
        mutation_begin();result=mutation_finish(chfs_rename_directory(&root_fs, old_path, new_path),new_path);
    }
    chos_spin_unlock_irqrestore(&storage_lock, flags);
    return result;
}
int storage_make_directory(const char *path) {
    uint64_t flags = chos_spin_lock_irqsave(&storage_lock);
    int result=0;
    if(write_ready_locked()){
        mutation_begin();result=mutation_finish(chfs_make_directory(&root_fs, path),path);
    }
    chos_spin_unlock_irqrestore(&storage_lock, flags);
    return result;
}
int storage_remove_directory(const char *path) {
    uint64_t flags = chos_spin_lock_irqsave(&storage_lock);
    int result=0;
    if(write_ready_locked()){
        mutation_begin();result=mutation_finish(chfs_remove_directory(&root_fs, path),NULL);
    }
    chos_spin_unlock_irqrestore(&storage_lock, flags);
    return result;
}
int storage_is_directory(const char *path) {
    size_t i;
    uint64_t flags = chos_spin_lock_irqsave(&storage_lock);
    int result = fs_ready && chfs_is_directory(&root_fs, path);
    if (!result && path != NULL && romfs_path_is_valid(path)) {
        for (i = 0u; i < romfs_file_count(); ++i) {
            chos_romfs_file_t file;
            if (romfs_get_at(i, &file) && path_has_child(path, file.path)) { result = 1; break; }
        }
    }
    chos_spin_unlock_irqrestore(&storage_lock, flags);
    return result;
}
int storage_sync(void) {
    uint64_t flags = chos_spin_lock_irqsave(&storage_lock);
    operation_begin();
    int result = fs_ready && filesystem_disk && filesystem_disk->flush && chfs_sync(&root_fs);
    chos_spin_unlock_irqrestore(&storage_lock, flags);
    return result;
}
int storage_get_at(size_t index, chos_chfs_entry_t *out) {
    uint64_t flags = chos_spin_lock_irqsave(&storage_lock);
    int result = fs_ready && chfs_get_at(&root_fs, index, out);
    chos_spin_unlock_irqrestore(&storage_lock, flags);
    return result;
}
void storage_get_stats(chos_chfs_stats_t *out) {
    uint64_t flags;
    if (out == 0) return;
    flags = chos_spin_lock_irqsave(&storage_lock);
    chfs_get_stats(fs_ready ? &root_fs : 0, out);
    chos_spin_unlock_irqrestore(&storage_lock, flags);
}

void storage_detach(void){
    uint64_t flags=chos_spin_lock_irqsave(&storage_lock);
    disk_ready=0;fs_ready=0;filesystem_disk=0;format_allowed=0;
    chos_storage_versions_invalidate(&file_versions);
    chos_spin_unlock_irqrestore(&storage_lock,flags);
}

static int read_snapshot_locked(const char *path,void *buffer,size_t capacity,size_t *bytes,chos_storage_lease_t *lease,int require_identity){
    if(!path||!bytes||!lease)return -CHOS_ERRNO_EINVAL;
    int result=-CHOS_ERRNO_EIO;
    if(!fs_ready)goto done;
    size_t index=catalogue_index(path);
    if(index==SIZE_MAX){result=-CHOS_ERRNO_ENOENT;goto done;}
    if(chfs_entry_is_directory(&root_fs.entries[index])){result=-CHOS_ERRNO_EISDIR;goto done;}
    if(root_fs.entries[index].size_bytes>capacity){result=-CHOS_ERRNO_E2BIG;goto done;}
    if(root_fs.entries[index].size_bytes&&!buffer){result=-CHOS_ERRNO_EINVAL;goto done;}
    chos_storage_lease_t value={0};
    if(!chos_storage_versions_get(&file_versions,index,&value)&&require_identity)goto done;
    size_t amount=0;operation_begin();
    if(!chfs_read_file(&root_fs,path,buffer,capacity,&amount)||operation_failed)goto done;
    *lease=value;*bytes=amount;result=0;
done:
    return result;
}
int storage_read_snapshot(const char *path,void *buffer,size_t capacity,size_t *bytes,chos_storage_lease_t *lease){
    uint64_t flags=chos_spin_lock_irqsave(&storage_lock);
    int result=read_snapshot_locked(path,buffer,capacity,bytes,lease,1);
    chos_spin_unlock_irqrestore(&storage_lock,flags);return result;
}
int storage_read_view(const char *path,void *buffer,size_t capacity,size_t *bytes,chos_storage_lease_t *lease){
    uint64_t flags=chos_spin_lock_irqsave(&storage_lock);
    int result=read_snapshot_locked(path,buffer,capacity,bytes,lease,0);
    chos_spin_unlock_irqrestore(&storage_lock,flags);return result;
}
int storage_file_metadata(const char *path,chos_file_metadata_t *out){
    if(!path||!out)return -CHOS_ERRNO_EINVAL;
    if(!(path[0]=='/'&&!path[1])&&!romfs_path_is_valid(path))return -CHOS_ERRNO_EINVAL;
    uint64_t flags=chos_spin_lock_irqsave(&storage_lock);int result=-CHOS_ERRNO_ENOENT;
    if(!fs_ready)goto done;
    size_t index=catalogue_index(path);
    chos_file_metadata_t value={.version=CHOS_FILE_METADATA_VERSION,.size_bytes=sizeof(value),
        .identity={.source=CHOS_FILE_SOURCE_CHFS}};
    if(index==SIZE_MAX){
        /* Legacy implicit directories and the physical root have no record
         * identity. Report the real directory without inventing an inode. */
        if(!chfs_is_directory(&root_fs,path))goto done;
        value.flags=CHOS_FILE_META_DIRECTORY;
        if(!write_ready_locked())value.flags|=CHOS_FILE_META_READ_ONLY;
        *out=value;result=0;goto done;
    }
    chos_chfs_entry_t *entry=&root_fs.entries[index];
    if(chfs_entry_is_directory(entry))value.flags=CHOS_FILE_META_DIRECTORY;
    else{value.flags=CHOS_FILE_META_REGULAR|CHOS_FILE_META_SIZE_VALID;value.length=entry->size_bytes;}
    if(!write_ready_locked())value.flags|=CHOS_FILE_META_READ_ONLY;
    chos_storage_lease_t lease;
    if(chos_storage_versions_get(&file_versions,index,&lease)){
        value.identity.mount=lease.mount;value.identity.object=lease.object;
        value.source_revision=lease.revision;value.flags|=CHOS_FILE_META_ID_VALID;
    }
    *out=value;result=0;
done:
    chos_spin_unlock_irqrestore(&storage_lock,flags);return result;
}
int storage_open_snapshot(const char *path,unsigned options,void *buffer,size_t capacity,
                          size_t *bytes,chos_storage_lease_t *lease){
 if(!path||!buffer||!capacity||capacity>CHOS_FILE_WRITE_MAX_BYTES||!bytes||!lease||
    (options&~CHOS_FILE_WRITE_FLAGS)||
    ((options&CHOS_FILE_WRITE_EXCLUSIVE)&&!(options&CHOS_FILE_WRITE_CREATE)))return -CHOS_ERRNO_EINVAL;
 if(path[0]=='/'&&!path[1])return -CHOS_ERRNO_EISDIR;
 if(!romfs_path_is_valid(path))return -CHOS_ERRNO_EINVAL;
 size_t length=0;while(path[length])if(++length>=CHOS_CHFS_PATH_MAX)return -CHOS_ERRNO_EINVAL;
 uint64_t flags=chos_spin_lock_irqsave(&storage_lock);
 int result=-CHOS_ERRNO_EIO;
 if(!fs_ready)goto done;
 if(chfs_is_directory(&root_fs,path)){result=-CHOS_ERRNO_EISDIR;goto done;}
 if(!write_ready_locked()){result=-CHOS_ERRNO_EROFS;goto done;}
 size_t index=catalogue_index(path);
 if(index!=SIZE_MAX){
  if(chfs_entry_is_directory(&root_fs.entries[index])){result=-CHOS_ERRNO_EISDIR;goto done;}
  if(options&CHOS_FILE_WRITE_EXCLUSIVE){result=-CHOS_ERRNO_EEXIST;goto done;}
  if(!(options&CHOS_FILE_WRITE_TRUNCATE)&&root_fs.entries[index].size_bytes>capacity){result=-CHOS_ERRNO_E2BIG;goto done;}
 }else{
  if(!(options&CHOS_FILE_WRITE_CREATE)){result=-CHOS_ERRNO_ENOENT;goto done;}
  char parent[CHOS_CHFS_PATH_MAX];size_t slash=0;
  for(size_t i=0;i<length;i++)if(path[i]=='/')slash=i;
  size_t parent_length=slash?slash:1;
  for(size_t i=0;i<parent_length;i++)parent[i]=path[i];
  parent[parent_length]=0;
  if(!chfs_is_directory(&root_fs,parent)){result=-CHOS_ERRNO_ENOENT;goto done;}
 }
 if(index==SIZE_MAX||(options&CHOS_FILE_WRITE_TRUNCATE)){
  mutation_begin();
  if(!mutation_finish(chfs_write_file(&root_fs,path,NULL,0),path)){
   result=operation_failed?-CHOS_ERRNO_EIO:-CHOS_ERRNO_ENOSPC;goto done;
  }
 }
 result=read_snapshot_locked(path,buffer,capacity,bytes,lease,1);
done:
 chos_spin_unlock_irqrestore(&storage_lock,flags);return result;
}
int storage_commit_snapshot(const char *path,const chos_storage_lease_t *lease,const void *data,size_t size,chos_storage_lease_t *updated){
    if(!path||!lease||!updated||(!data&&size)||size>CHOS_CHFS_FILE_MAX_BYTES)return -CHOS_ERRNO_EINVAL;
    uint64_t flags=chos_spin_lock_irqsave(&storage_lock);
    int result=-CHOS_ERRNO_EIO;
    if(!fs_ready)goto done;
    if(!chos_storage_versions_match(&file_versions,lease)||
       !romfs_path_equal(path,root_fs.entries[lease->index].path)){
        result=-CHOS_ERRNO_ESTALE;goto done;
    }
    if(chfs_entry_is_directory(&root_fs.entries[lease->index])){result=-CHOS_ERRNO_EISDIR;goto done;}
    if(!write_ready_locked()){result=-CHOS_ERRNO_EROFS;goto done;}
    mutation_begin();
    int success=mutation_finish(chfs_write_file(&root_fs,path,data,size),path);
    if(!success){result=operation_failed?-CHOS_ERRNO_EIO:-CHOS_ERRNO_ENOSPC;goto done;}
    chos_storage_lease_t value;
    if(!chos_storage_versions_get(&file_versions,lease->index,&value)){write_uncertain=1;goto done;}
    *updated=value;result=0;
done:
    chos_spin_unlock_irqrestore(&storage_lock,flags);return result;
}
