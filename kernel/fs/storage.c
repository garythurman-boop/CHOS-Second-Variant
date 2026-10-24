#include "chos/storage.h"
#include "chos/virtio_blk.h"
#include "chos/romfs.h"
#include "chos/spinlock.h"
#include "chos/gpt.h"

static chos_block_device_t boot_disk; /* whole physical device */
static chos_block_device_t gpt_fs_disk; /* bounded CHOS partition view, if present */
static chos_block_slice_context_t gpt_fs_context;
static chos_gpt_partition_t gpt_partitions[CHOS_GPT_MAX_PARTITIONS];
static chos_block_device_t *filesystem_disk;
static chos_chfs_t root_fs;
static int disk_ready;
static int fs_ready;
static int format_allowed;
static chos_gpt_error_t last_gpt_result = CHOS_GPT_ERR_ARGUMENT;
static chos_spinlock_t storage_lock = CHOS_SPINLOCK_INITIALIZER;

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
    filesystem_disk = &boot_disk;
    format_allowed = 1; /* raw unpartitioned CHFS images remain supported for QEMU */
    if (chfs_mount(&root_fs, &boot_disk)) {
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
            if (chfs_mount(&root_fs, filesystem_disk)) { fs_ready = 1; mounted = 1; break; }
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
const char *storage_error(void) { return virtio_blk_last_error(); }
int storage_block_ready(void) { return disk_ready; }
int storage_mounted(void) { return fs_ready; }
int storage_format(void) {
    uint64_t flags = chos_spin_lock_irqsave(&storage_lock);
    int result = disk_ready && format_allowed && filesystem_disk != NULL &&
                 filesystem_disk->write != NULL && chfs_format(filesystem_disk);
    fs_ready = result ? chfs_mount(&root_fs, filesystem_disk) : 0;
    chos_spin_unlock_irqrestore(&storage_lock, flags);
    return fs_ready;
}
int storage_mount(void) {
    uint64_t flags = chos_spin_lock_irqsave(&storage_lock);
    fs_ready = disk_ready && filesystem_disk != NULL && chfs_mount(&root_fs, filesystem_disk);
    chos_spin_unlock_irqrestore(&storage_lock, flags);
    return fs_ready;
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
    int result = fs_ready && chfs_read_file(&root_fs, path, buffer, capacity, bytes_read);
    chos_spin_unlock_irqrestore(&storage_lock, flags);
    return result;
}
int storage_write_file(const char *path, const void *data, size_t size) {
    uint64_t flags = chos_spin_lock_irqsave(&storage_lock);
    int result = fs_ready && chfs_write_file(&root_fs, path, data, size);
    chos_spin_unlock_irqrestore(&storage_lock, flags);
    return result;
}
int storage_remove_file(const char *path) {
    uint64_t flags = chos_spin_lock_irqsave(&storage_lock);
    int result = fs_ready && chfs_remove_file(&root_fs, path);
    chos_spin_unlock_irqrestore(&storage_lock, flags);
    return result;
}
int storage_rename_file(const char *old_path, const char *new_path) {
    uint64_t flags = chos_spin_lock_irqsave(&storage_lock);
    int result = fs_ready && chfs_rename_file(&root_fs, old_path, new_path);
    chos_spin_unlock_irqrestore(&storage_lock, flags);
    return result;
}
int storage_rename_directory(const char *old_path, const char *new_path) {
    uint64_t flags = chos_spin_lock_irqsave(&storage_lock);
    int result = fs_ready && chfs_rename_directory(&root_fs, old_path, new_path);
    chos_spin_unlock_irqrestore(&storage_lock, flags);
    return result;
}
int storage_make_directory(const char *path) {
    uint64_t flags = chos_spin_lock_irqsave(&storage_lock);
    int result = fs_ready && chfs_make_directory(&root_fs, path);
    chos_spin_unlock_irqrestore(&storage_lock, flags);
    return result;
}
int storage_remove_directory(const char *path) {
    uint64_t flags = chos_spin_lock_irqsave(&storage_lock);
    int result = fs_ready && chfs_remove_directory(&root_fs, path);
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
    int result = fs_ready && chfs_sync(&root_fs);
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
