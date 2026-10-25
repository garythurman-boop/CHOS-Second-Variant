"""Verify QEMU symbols against the kernel booted from an ISO or installed ESP.

This is read-only test infrastructure. It prevents an independent rebuild from
turning stale symbol offsets into misleading guest-state test failures.
"""
import argparse
import hashlib
import json
import pathlib
import struct
import uuid
import zlib
from test_mkuefi_iso import fat_find,read_fat_file,iso


def embedded_kernel_bytes(path):
    data=pathlib.Path(path).read_bytes()
    try:
        iso.validate_iso(data)
        catalog=struct.unpack_from('<I',data,17*2048+71)[0]
        esp_lba=struct.unpack_from('<I',data,catalog*2048+40)[0]
        esp=data[esp_lba*2048:]
        efi=fat_find(esp,'EFI')[0]
        chos=fat_find(esp,'CHOS',efi)[0]
        cluster,size,_=fat_find(esp,'CHOS.ELF',chos)
        return read_fat_file(esp,cluster,size)
    except (AssertionError,ValueError,struct.error,IndexError) as error:
        raise AssertionError('Cannot inspect embedded CHOS kernel in ISO '+str(path)+': '+str(error)) from error


def verify_iso_kernel(iso_path,kernel_path):
    embedded=embedded_kernel_bytes(iso_path)
    supplied=pathlib.Path(kernel_path).read_bytes()
    actual=hashlib.sha256(embedded).hexdigest()
    expected=hashlib.sha256(supplied).hexdigest()
    if embedded!=supplied:
        raise AssertionError('ISO embedded kernel differs from supplied symbols: '
                             +actual+' != '+expected+'. Finish/rebuild the ISO before testing.')
    return {'iso_embedded_kernel_sha256':actual,'kernel_symbols_match_iso':True}


def installed_kernel_bytes(path):
    """Read EFI/CHOS/CHOS.ELF through checked GPT/FAT32, without mounting a disk."""
    file_size=pathlib.Path(path).stat().st_size
    with pathlib.Path(path).open('rb') as image:
        def read(offset,size):
            assert offset>=0 and size>=0 and size<=file_size-offset,'disk extent outside image'
            image.seek(offset);data=image.read(size)
            assert len(data)==size,'short image read'
            return data
        assert file_size%512==0 and file_size>=34*512,'invalid GPT image size'
        header=bytearray(read(512,512))
        assert header[:8]==b'EFI PART','missing GPT header'
        header_size=struct.unpack_from('<I',header,12)[0]
        expected_crc=struct.unpack_from('<I',header,16)[0]
        assert 92<=header_size<=512,'invalid GPT header size'
        struct.pack_into('<I',header,16,0)
        assert zlib.crc32(header[:header_size])&0xffffffff==expected_crc,'invalid GPT header CRC'
        entries_lba,count,entry_size,entries_crc=struct.unpack_from('<QIII',header,72)
        assert 0<count<=4096 and 128<=entry_size<=1024,'unsupported GPT entry geometry'
        entries=read(entries_lba*512,count*entry_size)
        assert zlib.crc32(entries)&0xffffffff==entries_crc,'invalid GPT entry CRC'
        esp_type=uuid.UUID('c12a7328-f81f-11d2-ba4b-00a0c93ec93b').bytes_le
        partitions=[]
        for offset in range(0,len(entries),entry_size):
            if entries[offset:offset+16]==esp_type:
                first,last=struct.unpack_from('<QQ',entries,offset+32)
                assert first>=34 and first<=last<file_size//512,'invalid ESP extent'
                partitions.append((first,last))
        assert len(partitions)==1,'expected one EFI System Partition'
        first,last=partitions[0];esp_base=first*512;esp_bytes=(last-first+1)*512
        boot=read(esp_base,512)
        assert boot[510:512]==b'\x55\xaa' and boot[82:87]==b'FAT32','ESP is not FAT32'
        sector_size=struct.unpack_from('<H',boot,11)[0];spc=boot[13]
        reserved=struct.unpack_from('<H',boot,14)[0];fat_count=boot[16]
        total=struct.unpack_from('<I',boot,32)[0];fat_sectors=struct.unpack_from('<I',boot,36)[0]
        root=struct.unpack_from('<I',boot,44)[0]&0x0fffffff
        assert sector_size==512 and spc and not(spc&(spc-1)) and spc<=128,'invalid FAT32 cluster geometry'
        assert reserved and fat_count in (1,2) and fat_sectors and fat_sectors*512<=16*1024*1024,'invalid FAT32 FAT geometry'
        data_start=reserved+fat_count*fat_sectors
        assert data_start<total and total*512<=esp_bytes,'invalid FAT32 data extent'
        clusters=(total-data_start)//spc
        assert 65525<=clusters<=0x0ffffff5 and clusters+2<=fat_sectors*512//4,'invalid FAT32 cluster count'
        flags=struct.unpack_from('<H',boot,40)[0]
        active=(flags&15) if flags&128 else 0
        assert active<fat_count,'invalid active FAT32 table'
        fat=read(esp_base+(reserved+active*fat_sectors)*512,fat_sectors*512)
        cluster_bytes=spc*512
        def chain(cluster,maximum):
            seen=set();amount=0
            while cluster<0x0ffffff8:
                assert 2<=cluster<clusters+2 and cluster not in seen,'invalid/cyclic FAT32 chain'
                assert amount+cluster_bytes<=maximum,'FAT32 chain exceeds fixture bound'
                seen.add(cluster);amount+=cluster_bytes
                yield read(esp_base+(data_start+(cluster-2)*spc)*512,cluster_bytes)
                cluster=struct.unpack_from('<I',fat,cluster*4)[0]&0x0fffffff
        def find(directory,name,want_directory):
            target=iso.short_name(name)
            for data in chain(directory,1024*1024):
                for offset in range(0,len(data),32):
                    entry=data[offset:offset+32]
                    if entry[0]==0:raise AssertionError('FAT32 entry not found: '+name)
                    if entry[0]==0xe5 or entry[11]==0x0f:continue
                    if entry[:11]!=target:continue
                    assert bool(entry[11]&16)==want_directory,'unexpected FAT32 entry kind'
                    cluster=((struct.unpack_from('<H',entry,20)[0]<<16)|struct.unpack_from('<H',entry,26)[0])&0x0fffffff
                    return cluster,struct.unpack_from('<I',entry,28)[0]
            raise AssertionError('FAT32 entry not found: '+name)
        efi,_=find(root,'EFI',True);chos,_=find(efi,'CHOS',True)
        kernel,size=find(chos,'CHOS.ELF',False)
        assert 0<size<=256*1024*1024,'invalid installed kernel file size'
        result=bytearray()
        for chunk in chain(kernel,((size+cluster_bytes-1)//cluster_bytes)*cluster_bytes):
            result+=chunk
            if len(result)>=size:return bytes(result[:size])
        raise AssertionError('installed kernel FAT32 chain ended early')


def verify_installed_kernel(disk_path,kernel_path):
    embedded=installed_kernel_bytes(disk_path)
    supplied=pathlib.Path(kernel_path).read_bytes()
    actual=hashlib.sha256(embedded).hexdigest();expected=hashlib.sha256(supplied).hexdigest()
    if embedded!=supplied:
        raise AssertionError('Installed ESP kernel differs from supplied symbols: '+actual+' != '+expected)
    return {'installed_esp_kernel_sha256':actual,'kernel_symbols_match_installed_esp':True}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('iso',type=pathlib.Path)
    parser.add_argument('--kernel',type=pathlib.Path)
    parser.add_argument('--extract',type=pathlib.Path)
    parser.add_argument('--installed',action='store_true',help='inspect a GPT/FAT32 installed image instead of an ISO')
    options=parser.parse_args()
    if options.extract:
        options.extract.write_bytes((installed_kernel_bytes if options.installed else embedded_kernel_bytes)(options.iso))
    if options.kernel:
        print(json.dumps((verify_installed_kernel if options.installed else verify_iso_kernel)(options.iso,options.kernel),indent=2))
    elif not options.extract:
        print(hashlib.sha256((installed_kernel_bytes if options.installed else embedded_kernel_bytes)(options.iso)).hexdigest())
