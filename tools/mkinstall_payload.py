#!/usr/bin/env python3
"""Encode the prepared ESP as bounded sparse sector runs for the on-device installer."""
import argparse,struct,zlib
from pathlib import Path

def encode(image):
    # The prepared builder uses the same pure planner's fixed 64 MiB ESP.
    with image.open('rb') as f:
        f.seek(2048*512);esp=f.read(131072*512)
    if len(esp)!=131072*512 or esp[510:512]!=b'\x55\xaa':raise ValueError('Missing prepared FAT32 ESP')
    body=bytearray();runs=0;index=0
    while index<131072:
        if not any(esp[index*512:(index+1)*512]):index+=1;continue
        start=index
        while index<131072 and index-start<128 and any(esp[index*512:(index+1)*512]):index+=1
        body.extend(struct.pack('<II',start,index-start));body.extend(esp[start*512:index*512]);runs+=1
    result=struct.pack('<8sIIII',b'CHOSINS1',512,131072,runs,zlib.crc32(body))+body
    if len(result)>16*1024*1024:raise ValueError('Installer payload exceeds loader cap')
    return result
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--image',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.write_bytes(encode(a.image))
