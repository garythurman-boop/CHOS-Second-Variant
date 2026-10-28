"""Bounded host reader for pinned upstream CmnD packages, not a guest data API."""
import hashlib, pathlib, struct

def resources(data, inventory):
    def require(condition, message):
        if not condition: raise ValueError(message)
    require(len(data)>=24 and data[2:4]==b'\xda\x27', 'invalid package header')
    require(data[8:12]==b'\0\0\2\0' and data[12:20]==b'CmnD\1\0\0\0', 'unsupported package format')
    header = struct.unpack_from('<H', data)[0]
    require(24<=header<=len(data)-4, 'header size outside package')
    body = memoryview(data)[header:]
    count = struct.unpack_from('<I', body)[0]
    require(count==len(inventory) and 0<count<=65536, 'resource count mismatch')
    table_end = 4+count*8
    require(table_end<len(body), 'truncated resource table')
    pairs = [struct.unpack_from('<II', body, 4+i*8) for i in range(count)]
    first_data = pairs[0][1]
    require(table_end<=first_data<len(body), 'invalid first data offset')
    result = {}; previous_name = ''; previous_name_end = table_end
    for i, (name_offset, data_offset) in enumerate(pairs):
        require(previous_name_end<=name_offset<first_data, 'overlapping or invalid resource name')
        start = header+name_offset
        end = data.find(b'\0', start, header+first_data)
        require(end>=start, 'unterminated resource name')
        try: name = data[start:end].decode('ascii')
        except UnicodeDecodeError as error: raise ValueError('non-ASCII package name') from error
        require(name.startswith('icudt79l/'), 'unexpected package prefix')
        name = name[len('icudt79l/'):]
        path = pathlib.PurePosixPath(name)
        require(name and not path.is_absolute() and '..' not in path.parts and '.' not in path.parts, 'unsafe resource path')
        require(name>previous_name and name in inventory and name not in result, 'unordered or unknown resource')
        previous_name = name; previous_name_end = end-header+1
        limit = pairs[i+1][1] if i+1<count else len(body)
        size = inventory[name]['bytes']
        require(data_offset%16==0 and first_data<=data_offset<limit<=len(body), 'overlapping or invalid payload')
        require(size>0 and data_offset+size<=limit and limit-data_offset-size<16, 'resource length mismatch')
        payload = body[data_offset:data_offset+size]
        require(hashlib.sha256(payload).hexdigest()==inventory[name]['sha256'], 'resource SHA256 mismatch: '+name)
        result[name] = payload
    require(result.keys()==inventory.keys(), 'resource membership mismatch')
    return result
