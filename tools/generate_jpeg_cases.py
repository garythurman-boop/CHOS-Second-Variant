#!/usr/bin/env python3
"""Freeze selected upstream JPEG pixel regressions for native consumption.

Historical upstream MD5 values are regression identifiers, not authentication.
Source provenance and every original file are separately SHA256 verified.
"""
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCALES = ['2_1', '15_8', '13_8', '11_8', '9_8', '7_8', '3_4',
          '5_8', '1_2', '3_8', '1_4', '1_8']
SELECTED = ['rgb-islow', '422-ifast', '440-islow', '422m-ifast',
            '420-q100-ifast-prog', '420m-q100-ifast-prog', 'gray-islow',
            'gray-islow-rgb', '3x2-ifast-prog'] + [
                '420m-islow-' + scale for scale in SCALES]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def fnv(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return value


def pnm(data):
    """Parse binary P5/P6; never strip whitespace-valued raster samples."""
    position = 0

    def token():
        nonlocal position
        while position < len(data):
            if data[position] in b' \t\r\n\v\f':
                position += 1
            elif data[position] == ord('#'):
                newline = data.find(b'\n', position)
                if newline < 0:
                    raise ValueError('Unterminated PNM header comment')
                position = newline + 1
            else:
                break
        start = position
        while position < len(data) and data[position] not in b' \t\r\n\v\f#':
            position += 1
        if position == start:
            raise ValueError('Missing PNM header token')
        return data[start:position]

    magic = token()
    if magic not in (b'P5', b'P6'):
        raise ValueError('Only binary grayscale/RGB reference images are supported')
    width, height, maximum = int(token()), int(token()), int(token())
    if width <= 0 or height <= 0 or maximum != 255:
        raise ValueError('Invalid dimensions or non-8-bit reference raster')
    if position == len(data) or data[position] not in b' \t\r\n\v\f':
        raise ValueError('Missing PNM raster delimiter')
    # Upstream djpeg writes one '\n'. Consuming more could remove a real first
    # sample equal to whitespace and silently corrupt the reference.
    pixels = data[position + 1:]
    components = 3 if magic == b'P6' else 1
    if len(pixels) != width * height * components:
        raise ValueError('Reference raster has the wrong exact byte length')
    return width, height, components, pixels


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host-build', type=pathlib.Path, required=True)
    parser.add_argument('--out', type=pathlib.Path, required=True)
    parser.add_argument('--ctest', default='ctest')
    options = parser.parse_args()
    subprocess.run(['python3', str(ROOT / 'tools/verify_native_jpeg_sources.py')], check=True)
    build = options.host_build.resolve()
    source = ROOT / 'third_party/libjpeg-turbo'
    cache = {}
    for line in (build / 'CMakeCache.txt').read_text().splitlines():
        if '=' in line and ':' in line.split('=', 1)[0]:
            key, value = line.split('=', 1)
            cache[key.split(':', 1)[0]] = value
    if pathlib.Path(cache.get('CMAKE_HOME_DIRECTORY', '')).resolve() != source:
        raise ValueError('Host tools were configured from a different source tree')
    for key, expected in [('WITH_SIMD', 'OFF'), ('WITH_TESTS', 'ON'),
                          ('WITH_TOOLS', 'ON'), ('ENABLE_STATIC', 'ON')]:
        if cache.get(key) != expected:
            raise ValueError('Reference build requires ' + key + '=' + expected)
    ctest = shutil.which(options.ctest) or options.ctest
    metadata = subprocess.check_output([ctest, '--test-dir', str(build),
                                        '--show-only=json-v1'])
    tests = {test['name']: test for test in json.loads(metadata)['tests']}
    out = options.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    assets = out / 'assets'
    assets.mkdir()
    records, declarations, initializers = [], [], []
    assembly = ['.section .rodata.jpeg_fixtures,"a"']
    frozen = {}

    def asset(data):
        digest = sha(data)
        if digest not in frozen:
            name = 'jpeg_fixture_' + str(len(frozen))
            path = assets / (digest + '.jpg')
            path.write_bytes(data)
            assembly.extend(['.balign 8', '.global ' + name, name + ':',
                             '.incbin ' + json.dumps(str(path))])
            declarations.append('extern const unsigned char ' + name + '[];')
            frozen[digest] = (name, str(path.relative_to(out)))
        return frozen[digest]

    def capture(name, command, reference, expected_md5=None, comparison=None):
        # This is intentionally a narrow argument adapter. New test options
        # must be implemented explicitly rather than silently ignored.
        args = command[1:-1]
        dct_fast, fancy, numerator, denominator = 0, 1, 1, 1
        i = 0
        while i < len(args):
            arg = args[i]
            if arg == '-dct':
                value = args[i + 1]
                if value not in ('int', 'fast'):
                    raise ValueError('Unsupported DCT mode: ' + value)
                dct_fast = int(value == 'fast'); i += 2
            elif arg == '-scale':
                numerator, denominator = map(int, args[i + 1].split('/')); i += 2
            elif arg in ('-outfile', '-icc'):
                i += 2
            elif arg == '-nosmooth':
                fancy = 0; i += 1
            elif arg in ('-ppm', '-rgb'):
                i += 1
            else:
                raise ValueError('Unsupported reference option: ' + arg)
        encoded_path = pathlib.Path(command[-1])
        if not encoded_path.is_absolute():
            encoded_path = build / encoded_path
        encoded_path = encoded_path.resolve()
        if not (encoded_path.is_relative_to(source) or encoded_path.is_relative_to(build)):
            raise ValueError('Encoded input is outside verified source/reference build')
        encoded = encoded_path.read_bytes()
        image = reference.read_bytes()
        width, height, components, pixels = pnm(image)
        symbol, relative = asset(encoded)
        value = fnv(pixels)
        initializers.append('{' + ','.join([
            json.dumps(name), symbol, str(len(encoded)), str(width), str(height),
            str(components), str(dct_fast), str(fancy), str(numerator),
            str(denominator), 'UINT64_C(' + str(value) + ')']) + '}')
        record = {'name': name, 'asset': relative, 'encoded_sha256': sha(encoded),
                  'encoded_bytes': len(encoded), 'reference_PNM_sha256': sha(image),
                  'pixel_sha256': sha(pixels), 'pixel_FNV64': hex(value),
                  'width': width, 'height': height, 'components': components,
                  'dct_fast': dct_fast, 'fancy': fancy, 'scale_num': numerator,
                  'scale_denom': denominator, 'reference_command': command,
                  'historical_upstream_MD5': expected_md5,
                  'comparison_command': comparison,
                  'reference_kind': 'upstream historical pixel regression' if expected_md5
                                    else 'generic host decode of original signed arithmetic image'}
        records.append(record)

    for suffix in SELECTED:
        name = 'djpeg-static-' + suffix
        command = tests[name]['command']
        comparison = tests[name + '-cmp']['command']
        if pathlib.Path(command[0]).resolve() != build / 'djpeg-static':
            raise ValueError('Unexpected decoder executable')
        reference = build / command[command.index('-outfile') + 1]
        if not reference.resolve().is_relative_to(build):
            raise ValueError('Reference output is outside build')
        expected = comparison[1]
        if pathlib.Path(comparison[2]).name != reference.name:
            raise ValueError('Comparison identifies a different output')
        if hashlib.md5(reference.read_bytes(), usedforsecurity=False).hexdigest() != expected:
            raise ValueError('Original upstream regression digest mismatch: ' + name)
        # Execute the original comparator as well as checking its historical
        # expected value. This does not replace source SHA256 authentication.
        subprocess.run(comparison, cwd=build, check=True)
        capture(suffix, command, reference, expected, comparison)

    arithmetic = source / 'testimages/testimgari.jpg'
    reference = out / 'arithmetic-reference.ppm'
    command = [str(build / 'djpeg-static'), '-dct', 'int', '-ppm', '-outfile',
               str(reference), str(arithmetic)]
    subprocess.run(command, cwd=build, check=True)
    capture('arithmetic-full-islow', command, reference)
    (out / 'jpeg-fixtures.S').write_text('\n'.join(assembly) + '\n')
    (out / 'jpeg-cases.h').write_text('\n'.join(declarations) + '\n'
        'struct JPEGCase {const char *name;const unsigned char *data;size_t length;'
        'unsigned width,height,components,dct_fast,fancy,scale_num,scale_denom;uint64_t hash;};\n'
        'static const struct JPEGCase jpeg_cases[]={\n' + ',\n'.join(initializers) + '\n};\n')
    manifest = {'schema': 1, 'source_manifest_sha256': sha((ROOT / 'docs/dependencies/native-jpeg.json').read_bytes()),
                'reference': 'Generic non-SIMD upstream host tools checked against original historical goldens',
                'historical_upstream_cases': len(SELECTED), 'additional_host_reference_cases': 1,
                'historical_MD5_is_not_source_authentication': True,
                'ctest_metadata_sha256': sha(metadata),
                'reference_tools_sha256': {name: sha((build / name).read_bytes())
                                          for name in ['djpeg-static', 'test/md5cmp']},
                'cases': records}
    (out / 'cases.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('Generated', len(records), 'JPEG pixel cases from', len(frozen), 'frozen encoded assets')


if __name__ == '__main__':
    main()
