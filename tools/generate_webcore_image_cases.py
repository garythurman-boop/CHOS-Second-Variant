#!/usr/bin/env python3
"""Generate real WebCore image inputs and independent complete RGB frame references.

The PNG samples and premultiplication/composition are calculated directly.
JPEG reference samples come from verified generic upstream host tools and must
match the previously qualified exact output. No WebCore or Cairo renders a golden.
"""
import argparse
import hashlib
import json
import pathlib
import shutil
import struct
import subprocess
import sys
import zlib

sys.dont_write_bytecode = True
ROOT = pathlib.Path(__file__).resolve().parents[1]
from generate_jpeg_cases import pnm

JPEG_BGRA_SHA256 = '3eaaa5566770284a05f8d951c9df92159001c71ef9f6e87fc33579d1b1863c3a'
PNG_BGRA_SHA256 = '4bcfe7cfae94452f4e484516348ea4dbefdd3f2b0e110e5d17af8adb1ab9690c'
FRAME_SHA256 = [
    'd6181f1d4c783c65fae40eb30911207d432044bf303d228bb88297a84adda4f6',
    '2aefbb4496fc1891940e9ac8fdead78dff746ee157f754af2f781b741a99cf0a',
    '8f6aedf020ca962c4f638caffe1aef51884ca00d575fb6f05090bb2674e27b6e',
]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def fnv(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return value


def chunk(kind, data):
    return (struct.pack('>I', len(data)) + kind + data
            + struct.pack('>I', zlib.crc32(kind + data)))


def verify_reference(build):
    source = ROOT / 'third_party/libjpeg-turbo'
    cache = {}
    for line in (build / 'CMakeCache.txt').read_text().splitlines():
        if '=' in line and ':' in line.split('=', 1)[0]:
            key, value = line.split('=', 1)
            cache[key.split(':', 1)[0]] = value
    if pathlib.Path(cache.get('CMAKE_HOME_DIRECTORY', '')).resolve() != source:
        raise ValueError('JPEG reference was configured from a different source tree')
    for key, expected in [('WITH_SIMD', 'OFF'), ('ENABLE_STATIC', 'ON'),
                          ('WITH_TOOLS', 'ON'), ('WITH_TESTS', 'ON')]:
        if cache.get(key) != expected:
            raise ValueError('JPEG reference requires ' + key + '=' + expected)
    tool = build / 'djpeg-static'
    if tool.is_symlink() or not tool.is_file():
        raise ValueError('JPEG reference decoder must be a built regular executable')
    return tool


def compose(png, jpeg, epoch):
    """Independent integer source-over using known premultiplied samples."""
    background = [224, 230, 236 - (epoch - 1) * 4]
    rgb = bytearray(background * (512 * 256))
    left = 16 + (epoch - 1) * 8
    for y in range(64):
        for x in range(96):
            blue, green, red, alpha = png[(y * 96 + x) * 4:(y * 96 + x) * 4 + 4]
            for yy in range(2):
                for xx in range(2):
                    position = ((32 + y * 2 + yy) * 512 + left + x * 2 + xx) * 3
                    for channel, sample in enumerate([red, green, blue]):
                        product = background[channel] * (255 - alpha) + 128
                        multiplied = (product + (product >> 8)) >> 8
                        rgb[position + channel] = min(255, sample + multiplied)
    left = 270 - (epoch - 1) * 8
    for y in range(149):
        for x in range(227):
            blue, green, red, alpha = jpeg[(y * 227 + x) * 4:(y * 227 + x) * 4 + 4]
            if alpha != 255:
                raise ValueError('JPEG reference must be opaque')
            position = ((32 + y) * 512 + left + x) * 3
            rgb[position:position + 3] = bytes([red, green, blue])
    return bytes(rgb)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=pathlib.Path, required=True)
    parser.add_argument('--jpeg-reference-build', type=pathlib.Path, required=True)
    args = parser.parse_args()
    subprocess.run(['python3', str(ROOT / 'tools/verify_native_jpeg_sources.py')], check=True)
    build = args.jpeg_reference_build.resolve()
    tool = verify_reference(build)
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    width, height = 96, 64
    rgba = bytes(channel for y in range(height) for x in range(width)
                 for channel in [(x * 3 + y * 7) & 255, (x * 11 + y * 5) & 255,
                                 (x * 7 + y * 13) & 255, [0, 85, 170, 255][(x // 8 + y // 8) & 3]])
    scans = b''.join(b'\0' + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))
    encoded = (b'\x89PNG\r\n\x1a\n'
               + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
               + chunk(b'IDAT', zlib.compress(scans, 9)) + chunk(b'IEND', b''))
    (out / 'generated.png').write_bytes(encoded)
    png = bytes(channel for i in range(0, len(rgba), 4)
                for channel in [rgba[i + 2] * rgba[i + 3] // 255,
                                rgba[i + 1] * rgba[i + 3] // 255,
                                rgba[i] * rgba[i + 3] // 255, rgba[i + 3]])
    if sha(png) != PNG_BGRA_SHA256:
        raise ValueError('Generated PNG mathematical sample reference changed')
    (out / 'generated.bgra').write_bytes(png)
    source = ROOT / 'third_party/libjpeg-turbo/testimages/testorig.jpg'
    shutil.copyfile(source, out / 'original.jpg')
    command = [str(tool), '-dct', 'int', '-ppm', '-outfile',
               str(out / 'original.ppm'), str(out / 'original.jpg')]
    subprocess.run(command, check=True)
    jpeg_width, jpeg_height, components, raw = pnm((out / 'original.ppm').read_bytes())
    if (jpeg_width, jpeg_height, components) != (227, 149, 3):
        raise ValueError('Original JPEG reference geometry changed')
    jpeg = bytes(channel for i in range(0, len(raw), 3)
                 for channel in [raw[i + 2], raw[i + 1], raw[i], 255])
    if sha(jpeg) != JPEG_BGRA_SHA256:
        raise ValueError('Actual JPEG host output differs from qualified sample reference')
    (out / 'original.bgra').write_bytes(jpeg)

    assembly = ['.section .rodata.image_fixtures,"a"']
    header = ['#pragma once', '#include <stdint.h>', '#include <stddef.h>', 'extern "C" {']
    records = []
    for name, filename, pixels, w, h in [
            ('generated', 'generated.png', png, width, height),
            ('original', 'original.jpg', jpeg, jpeg_width, jpeg_height)]:
        assembly.extend(['.balign 8', '.global ' + name + '_start', name + '_start:',
                         '.incbin ' + json.dumps(str(out / filename)),
                         '.global ' + name + '_end', name + '_end:'])
        header.append('extern const unsigned char ' + name + '_start[], ' + name + '_end[];')
        records.append({'name': name, 'encoded_sha256': sha((out / filename).read_bytes()),
                        'BGRA_premultiplied_SHA256': sha(pixels), 'BGRA_FNV64': hex(fnv(pixels)),
                        'width': w, 'height': h})
    header.append('}')
    for record in records:
        header.extend(['static constexpr uint64_t ' + record['name'] + '_hash=UINT64_C('
                       + str(int(record['BGRA_FNV64'], 16)) + ');',
                       'static constexpr unsigned ' + record['name'] + '_width=' + str(record['width'])
                       + ', ' + record['name'] + '_height=' + str(record['height']) + ';'])
    (out / 'assets.S').write_text('\n'.join(assembly) + '\n')
    (out / 'assets.h').write_text('\n'.join(header) + '\n')
    frames = []
    for epoch in range(1, 4):
        pixels = compose(png, jpeg, epoch)
        if sha(pixels) != FRAME_SHA256[epoch - 1]:
            raise ValueError('Independent frame composition changed')
        (out / ('frame' + str(epoch) + '.rgb')).write_bytes(pixels)
        frames.append({'epoch': epoch, 'width': 512, 'height': 256,
                       'RGB_sha256': sha(pixels), 'bytes': len(pixels)})
    manifest = {'schema': 1, 'cases': records, 'frames': frames,
                'generator_sha256': sha(pathlib.Path(__file__).read_bytes()),
                'PNM_parser_sha256': sha((ROOT / 'tools/generate_jpeg_cases.py').read_bytes()),
                'jpeg_source_manifest_sha256': sha((ROOT / 'docs/dependencies/native-jpeg.json').read_bytes()),
                'jpeg_reference_build': str(build), 'jpeg_reference_CMakeCache_sha256': sha((build / 'CMakeCache.txt').read_bytes()),
                'jpeg_reference_tool_sha256': sha(tool.read_bytes()),
                'jpeg_reference_command': command,
                'jpeg_input_sha256': sha(source.read_bytes()),
                'limits': ['Known mathematical PNG encoded without gamma or color chunks;floor premultiplication',
                           'Original IJG JPEG plus actual generic upstream integer/fancy decode,checked against fixed complete-sample SHA256',
                           'Frame references use independent integer compositing;no Cairo or WebCore golden rendering',
                           'Component image qualification only;not complete browser or color-management conformance']}
    (out / 'assets.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('Generated real PNG/JPEG inputs and three independent complete-frame references')


if __name__ == '__main__':
    main()
