'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs/promises'),path=require('node:path');
const {Tools,schemas}=require('../core/tools.cjs');
const root=path.resolve(__dirname,'../test-output/music');
async function fixture(){await fs.mkdir(root,{recursive:true});return fs.mkdtemp(path.join(root,'case-'));}
test('music broker discovers its command without allowing installation writes',async()=>{
 assert(schemas.find(x=>x.name==='engine').input_schema.properties.command.enum.includes('music'));
 const t=new Tools({engine:process.execPath,project:{root:await fixture()},assertWrite:()=>{}});
 await assert.rejects(t.call('engine',{command:'music',subcommand:'setup'}),/installation step/);
 await assert.rejects(t.call('engine',{command:'music',subcommand:'status',options:{root:'elsewhere'}}),/Unsupported music option/);
});
test('music render, bank install and imports remain inside project and honor play locks',async()=>{
 const t=new Tools({engine:process.execPath,project:{root:await fixture()},assertWrite:()=>{}});
 for(const [subcommand,options] of [['render',{'output-dir':'../escaped'}],['import',{source:'C:/private/voice.wav'}],['pack-install',{directory:'../bank'}]])
  await assert.rejects(t.call('engine',{command:'music',subcommand,options}),/boundary|relative|escapes/);
 t.assertWrite=()=>{throw Error('Human play session active');};
 for(const subcommand of ['render','audition','transform','import','pack-install'])
  await assert.rejects(t.call('engine',{command:'music',subcommand}),/Human play session/);
});
for(const layout of ['source','portable'])test('music documentation discovery: '+layout,async()=>{
 const p=await fixture();let engine,names;
 if(layout==='source'){await fs.mkdir(path.join(p,'build'));await fs.mkdir(path.join(p,'docs'));engine=path.join(p,'build/langmake.exe');names=['30-music-and-sound-guide.md','31-audio-qualification.md'];}
 else{engine=path.join(p,'langmake.exe');names=['MUSIC-AND-SOUND.md','AUDIO-QUALIFICATION.md'];}
 for(const name of names)await fs.writeFile(path.join(p,layout==='source'?'docs':'',name),'Audio technical evidence, not artistic approval');
 const t=new Tools({engine,project:{root:p}}),index=await t.call('documentation');
 for(const name of names){assert(index.documents.includes(name));assert.match((await t.call('documentation',{path:name})).content,/not artistic approval/);}
});
