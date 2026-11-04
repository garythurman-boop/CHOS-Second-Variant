#!/usr/bin/env python3
"""Verify and embed the fixed original Test262 selection;never execute JavaScript."""
import argparse,hashlib,json,pathlib,re
ROOT=pathlib.Path(__file__).resolve().parents[1]
MANIFEST=ROOT/'docs/dependencies/test262-selected.json'
PIN='c990733f8d234dd333bd8a71c0c2eb871249b0e0'
def metadata(text):
    match=re.search(r'/\*---(.*?)---\*/',text,re.S)
    if not match:raise ValueError('Missing original Test262 frontmatter')
    front=match[1]
    def flow(field):
        lines=re.findall(r'^'+field+r':([^\n]*)',front,re.M)
        if not lines:return []
        if len(lines)!=1:raise ValueError('Duplicate '+field)
        value=re.fullmatch(r'\s*\[([^\]]*)\]\s*',lines[0])
        if not value:raise ValueError('Unsupported '+field+' metadata')
        result=[s.strip() for s in value[1].split(',') if s.strip()]
        if len(result)!=len(set(result)):raise ValueError('Duplicate '+field+' entry')
        return result
    flags=flow('flags');includes=flow('includes')
    if set(flags)-{'generated','noStrict','onlyStrict','async'}:raise ValueError('Unsupported execution flag')
    if {'noStrict','onlyStrict'}<=set(flags):raise ValueError('Conflicting strict modes')
    negative=''
    if re.search(r'^negative:',front,re.M):
        phase=re.search(r'^[ \t]+phase:[ \t]*(\S+)',front,re.M)
        kind=re.search(r'^[ \t]+type:[ \t]*(\S+)',front,re.M)
        if not phase or not kind or phase[1]!='parse' or kind[1]!='SyntaxError':
            raise ValueError('Unsupported negative test;not silently omitted')
        negative=kind[1]
    return {'flags':flags,'includes':includes,'negative':negative,
            'modes':1 if 'noStrict' in flags else (2 if 'onlyStrict' in flags else 3),
            'asynchronous':'async' in flags}
def checked_path(source,name):
    path=pathlib.PurePosixPath(name)
    if path.is_absolute() or any(p in {'.','..'} for p in path.parts) or str(path)!=name:
        raise ValueError('Unsafe source path')
    if name!='LICENSE' and not name.startswith(('test/','harness/')):raise ValueError('Unexpected source path')
    result=source/path
    if result.is_symlink() or not result.resolve().is_relative_to(source.resolve()):raise ValueError('Source escapes root')
    return result
def inputs(source,manifest):
    if manifest['commit']!=PIN or manifest['catalogue_truncated']:raise ValueError('Unreviewed upstream selection')
    records={item['path']:item for item in manifest['files']}
    if len(records)!=len(manifest['files']):raise ValueError('Duplicate source records')
    data={}
    for name,item in records.items():
        raw=checked_path(source,name).read_bytes()
        if len(raw)!=item['bytes'] or hashlib.sha256(raw).hexdigest()!=item['sha256']:
            raise ValueError('Changed pinned Test262 source: '+name)
        data[name]=raw.decode('utf-8')
    selected=manifest['selected_test_paths']
    if len(selected)!=88 or len(set(selected))!=88:raise ValueError('Changed selected test set')
    cases=[]
    for name in selected:
        if not name.startswith('test/'):raise ValueError('Not a test path')
        case={'path':name,**metadata(data[name])}
        for helper in case['includes']:
            if 'harness/'+helper not in data:raise ValueError('Missing original helper: '+helper)
        cases.append(case)
    return data,cases
def raw_literal(text):
    if '\0' in text:raise ValueError('Embedded NUL needs an explicitly length-aware API')
    number=0
    while ')TC262_'+str(number)+'"' in text:number+=1
    tag='TC262_'+str(number)
    return 'R"'+tag+'('+text+')'+tag+'"'
def header(data,cases):
    helpers=['sta.js','assert.js','doneprintHandle.js']
    helpers+=sorted({v for case in cases for v in case['includes']}-set(helpers))
    names={name:i for i,name in enumerate(helpers)}
    result='// Generated from pinned original TC39 sources;see selection manifest and LICENSE.\n'
    result+='struct Source {const char* name;const char* text;};\nstatic const Source helpers[]={\n'
    result+=''.join('{'+json.dumps(n)+','+raw_literal(data['harness/'+n])+'},\n' for n in helpers)+'};\n'
    result+='struct Case {const char* name;const char* text;unsigned modes;bool asynchronous;const char* negative;const int includes[8];};\nstatic const Case cases[]={\n'
    for case in cases:
        ids=[names[n] for n in case['includes']]
        if len(ids)>=8:raise ValueError('Too many original helpers')
        ids+=[-1]*(8-len(ids))
        result+='{'+json.dumps(case['path'])+','+raw_literal(data[case['path']])+','+str(case['modes'])+','+str(case['asynchronous']).lower()+','+json.dumps(case['negative'])+',{'+','.join(map(str,ids))+'}},\n'
    return result+'};\n'
def prepare(source,out):
    manifest=json.loads(MANIFEST.read_text());data,cases=inputs(source,manifest);generated=header(data,cases)
    out.mkdir(parents=True,exist_ok=False);(out/'cases.h').write_text(generated)
    (out/'cases.json').write_text(json.dumps(cases,indent=2)+'\n')
    report={'status':'PREPARED_NOT_EXECUTED','commit':PIN,'tests':len(cases),
            'variants':sum(2 if x['modes']==3 else 1 for x in cases),
            'manifest_sha256':hashlib.sha256(MANIFEST.read_bytes()).hexdigest(),
            'generated_header_sha256':hashlib.sha256(generated.encode()).hexdigest(),
            'selection_policy':manifest['policy']}
    (out/'inputs.json').write_text(json.dumps(report,indent=2)+'\n');return report
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=pathlib.Path,default=ROOT/'third_party/test262-subset');p.add_argument('--out',type=pathlib.Path,required=True);o=p.parse_args();print(json.dumps(prepare(o.source,o.out),indent=2))
