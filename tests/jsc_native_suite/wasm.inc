static bool wasm() {
 auto context=JSGlobalContextCreate(nullptr);if(!context)return false;
 const char* script=R"CHOSWASM((()=>{
  const check=(value,name)=>{if(!value)throw new Error(name);};
  check(typeof WebAssembly==='object','Wasm global');
  const addBytes=new Uint8Array([0,97,115,109,1,0,0,0,1,7,1,96,2,127,127,1,127,3,2,1,0,7,7,1,3,97,100,100,0,0,10,9,1,7,0,32,0,32,1,106,11]);
  check(WebAssembly.validate(addBytes),'valid module');
  const add=new WebAssembly.Instance(new WebAssembly.Module(addBytes)).exports.add;
  check(add(19,23)===42&&add(-1,1)===0&&add(2147483647,1)===-2147483648,'actual i32 add and wrap');
  const invalid=addBytes.slice();invalid[4]=2;check(!WebAssembly.validate(invalid),'invalid binary rejected');
  let rejected=false;try{new WebAssembly.Module(invalid);}catch(e){rejected=e instanceof WebAssembly.CompileError;}check(rejected,'CompileError');
  const loadBytes=new Uint8Array([0,97,115,109,1,0,0,0,1,6,1,96,1,127,1,127,3,2,1,0,5,4,1,1,1,2,7,14,2,4,114,101,97,100,0,0,3,109,101,109,2,0,10,9,1,7,0,32,0,40,2,0,11]);
  const exports=new WebAssembly.Instance(new WebAssembly.Module(loadBytes)).exports;
  new Uint32Array(exports.mem.buffer)[0]=0x12345678;check(exports.read(0)===0x12345678,'actual checked load');
  check(exports.read(65532)===0,'last in-bounds load');
  for(const offset of [65533,65536,-1]){let trapped=false;try{exports.read(offset);}catch(e){trapped=e instanceof WebAssembly.RuntimeError;}check(trapped,'actual bounds trap '+offset);}
  const old=exports.mem.buffer;check(exports.mem.grow(1)===1,'grow return');
  check(old.byteLength===0&&exports.mem.buffer.byteLength===131072,'old buffer detached and new length');
  check(new Uint32Array(exports.mem.buffer)[0]===0x12345678&&exports.read(65536)===0,'growth preserves contents and zeroes new page');
  let maximum=false;try{exports.mem.grow(1);}catch(e){maximum=e instanceof RangeError;}check(maximum,'memory maximum');
  const table=new WebAssembly.Table({element:'anyfunc',initial:1,maximum:2});table.set(0,add);
  check(table.get(0)(20,22)===42&&table.grow(1)===1&&table.length===2&&table.get(1)===null,'table/function/growth');
  let tableMaximum=false;try{table.grow(1);}catch(e){tableMaximum=e instanceof RangeError;}check(tableMaximum,'table maximum');
  return true;
 })())CHOSWASM";
 bool ok=evaluate(context,script,"chos-checked-wasm-fixture");JSGarbageCollect(context);JSGlobalContextRelease(context);
 std::puts(ok?"[ OK ] actual checked native Wasm arithmetic,memory growth,tables and bounds traps executed":"[FAIL] actual native Wasm fixture");return ok;
}
