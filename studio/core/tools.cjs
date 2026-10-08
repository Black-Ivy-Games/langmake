'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto'),{spawn}=require('node:child_process');
const hash=text=>crypto.createHash('sha256').update(text).digest('hex');
const RESERVED=new Set(['.git','.langmake','node_modules','.studio']);
function within(root,file){const relative=path.relative(root,file);return relative===''||(!relative.startsWith('..'+path.sep)&&relative!=='..'&&!path.isAbsolute(relative));}
async function scoped(root,relative,{write=false}={}){
 if(typeof relative!=='string'||!relative||relative.length>512||relative.includes('\0')||path.isAbsolute(relative)||/[:]/.test(relative))throw Error('Use a relative project path');
 const parts=relative.replaceAll('\\','/').split('/');if(parts.some(p=>p==='..'||RESERVED.has(p.toLowerCase())))throw Error('Path crosses a protected project boundary');
 const base=await fs.realpath(root),file=path.resolve(base,...parts);if(!within(base,file))throw Error('Path escapes project');
 let current=base;for(const part of parts.filter(p=>p&&p!=='.')){current=path.join(current,part);try{const info=await fs.lstat(current);if(info.isSymbolicLink())throw Error('Symbolic links and junctions are not available to agent tools');const real=await fs.realpath(current);if(!within(base,real))throw Error('Path escapes project');}catch(e){if(e.code==='ENOENT'&&write)break;throw e;}}
 return file;
}
async function listFiles(root,folder=''){const base=folder?await scoped(root,folder):await fs.realpath(root),files=[];
 async function walk(dir,depth){if(depth>16)throw Error('Directory exceeds 16 levels');for(const ent of await fs.readdir(dir,{withFileTypes:true})){if(RESERVED.has(ent.name.toLowerCase())||ent.isSymbolicLink())continue;const file=path.join(dir,ent.name);if(ent.isDirectory())await walk(file,depth+1);else if(ent.isFile()){const stat=await fs.stat(file);files.push({path:path.relative(root,file).split(path.sep).join('/'),bytes:stat.size});if(files.length>4096)throw Error('Project exceeds 4096 visible files');}}}await walk(base,0);return files.sort((a,b)=>a.path.localeCompare(b.path));}
async function readFile(root,file){const target=await scoped(root,file),stat=await fs.stat(target);if(stat.size>2*1024*1024)throw Error('Text file exceeds 2 MiB');const bytes=await fs.readFile(target);if(bytes.includes(0))throw Error('Binary file; use native asset inspection');const content=bytes.toString('utf8');return{path:file,content,revision:hash(bytes)};}
async function writeFile(root,file,content,expected,guard=()=>{}){if(typeof content!=='string'||Buffer.byteLength(content)>2*1024*1024)throw Error('Text content must be at most 2 MiB');const target=await scoped(root,file,{write:true});let previous=null;try{previous=hash(await fs.readFile(target));}catch(e){if(e.code!=='ENOENT')throw e;}
 if(expected!==previous)throw Error('File revision changed; read it again. New files require expected_revision:null');await fs.mkdir(path.dirname(target),{recursive:true});const tmp=target+'.studio-'+crypto.randomUUID()+'.tmp';await fs.writeFile(tmp,content);try{await scoped(root,file,{write:true});let current=null;try{current=hash(await fs.readFile(target));}catch(e){if(e.code!=='ENOENT')throw e;}if(current!==expected)throw Error('File changed during write preparation; read it again');guard();await fs.rename(tmp,target);}catch(e){await fs.rm(tmp,{force:true});throw e;}return{path:file,revision:hash(content),bytes:Buffer.byteLength(content)};}
function kill(child){if(!child||child.exitCode!==null)return;if(process.platform==='win32')spawn('taskkill',['/pid',String(child.pid),'/t','/f'],{windowsHide:true,stdio:'ignore'});else child.kill('SIGTERM');}
function execute(command,args,{cwd,signal,timeout=120000,onChunk,onStderr,env}={}){return new Promise((resolve,reject)=>{if(signal?.aborted)return reject(Error('Cancelled'));const child=spawn(command,args,{cwd,env:env?{...process.env,...env}:process.env,shell:false,windowsHide:true,stdio:['ignore','pipe','pipe']});let stdout='',stderr='',failed=false;const stop=()=>{failed=true;kill(child);};const timer=setTimeout(stop,timeout);signal?.addEventListener('abort',stop,{once:true});child.on('error',e=>{clearTimeout(timer);signal?.removeEventListener('abort',stop);reject(e);});child.stdout.setEncoding('utf8');child.stderr.setEncoding('utf8');child.stdout.on('data',b=>{stdout+=b.toString();onChunk?.(b.toString());if(stdout.length+stderr.length>8*1024*1024)stop();});child.stderr.on('data',b=>{stderr+=b.toString();onStderr?.(b.toString());if(stdout.length+stderr.length>8*1024*1024)stop();});child.on('close',code=>{clearTimeout(timer);signal?.removeEventListener('abort',stop);if(failed)return reject(Error(signal?.aborted?'Cancelled':'Process exceeded time or output budget'));resolve({code,stdout,stderr});});});}
const schemas=[
 ['list_files','List project files. Protected engine/session directories are excluded.',{folder:{type:'string'}}],
 ['read_file','Read a UTF-8 project file, returning content and revision.',{path:{type:'string'}},['path']],
 ['read_image','View a project PNG/JPEG, including native capture output. Requires a vision-capable model.',{path:{type:'string'}},['path']],
 ['documentation','List or read installed Langmake authoring documentation and prompt examples.',{path:{type:'string'}}],
 ['ask_human','Ask a focused design question when human direction is required.',{question:{type:'string'}},['question']],
 ['write_file','Create or replace a UTF-8 project file. Require read revision, or null for a new file.',{path:{type:'string'},content:{type:'string'},expected_revision:{type:['string','null']}},['path','content','expected_revision']],
 ['engine','Run bounded Langmake technical commands. Paths in options are project-relative. No shell.',{command:{type:'string',enum:['capabilities','schema','script-api','validate','check','inspect','simulate','profile','capture','asset','music']},options:{type:'object'},subcommand:{type:'string'}},['command']],
 ['memory_read','Read the durable human brief, decisions, tasks and handoff notes.',{}],
 ['memory_update','Replace structured durable project memory. Keep human decisions distinct from agent assumptions.',{brief:{type:'string'},decisions:{type:'array',items:{type:'string'}},tasks:{type:'array',items:{type:'string'}},handoff:{type:'string'}},['brief','decisions','tasks','handoff']]
].map(([name,description,properties,required=[]])=>({name,description,input_schema:{type:'object',properties,required,additionalProperties:false}}));
class Tools{
 constructor(options){Object.assign(this,options);this.pending=new Set();this.queue=Promise.resolve();}
 call(name,args={}){const pending=this.queue.catch(()=>{}).then(()=>{if(this.signal?.aborted)throw Error('Cancelled');return this.dispatch(name,args);});this.queue=pending;this.pending.add(pending);pending.then(()=>this.pending.delete(pending),()=>this.pending.delete(pending));return pending;}
 async drain(){await Promise.allSettled([...this.pending]);}
 async dispatch(name,args={}){this.emit?.({kind:'tool',name,args:name==='write_file'?{path:args.path,bytes:Buffer.byteLength(args.content||'')}:args});let result;
 if(name==='list_files')result=await listFiles(this.project.root,args.folder||'');
 else if(name==='read_file')result=await readFile(this.project.root,args.path);
 else if(name==='read_image'){const file=await scoped(this.project.root,args.path),info=await fs.stat(file);if(info.size>8*1024*1024)throw Error('Image exceeds8 MiB');const data=await fs.readFile(file);const png=data.subarray(0,8).equals(Buffer.from([137,80,78,71,13,10,26,10])),jpeg=data[0]===255&&data[1]===216&&data[2]===255;if(!png&&!jpeg)throw Error('Only PNG/JPEG images are supported');result={type:'image',mimeType:png?'image/png':'image/jpeg',data:data.toString('base64'),path:args.path};}
 else if(name==='documentation')result=await documentation(this.engine,args.path);
 else if(name==='ask_human')result=await this.ask([{id:'decision',question:args.question}]);
 else if(name==='write_file'){this.assertWrite();result=await writeFile(this.project.root,args.path,args.content,args.expected_revision,this.assertWrite);}
 else if(name==='memory_read')result=this.memory();
 else if(name==='memory_update'){this.assertWrite();if(JSON.stringify(args).length>32000)throw Error('Project memory exceeds 32 KiB');result=await this.saveMemory(args);}
 else if(name==='engine')result=await this.runEngine(args);
 else throw Error('Unknown Studio tool');this.emit?.({kind:'tool_result',name,result:result?.type==='image'?'Image attached: '+result.path:JSON.stringify(result).slice(0,12000)});return result;}
 async runEngine({command,options={},subcommand}){if(!schemas.find(x=>x.name==='engine').input_schema.properties.command.enum.includes(command))throw Error('Unsupported engine command');if(!this.engine)throw Error('Choose the Langmake executable in Settings');
 if(command==='music')return this.runMusic(subcommand,options);
 const allowed={capabilities:[],schema:[],'script-api':[],validate:[],check:[],inspect:['ids','role','visible','fields','offset','limit','live','check-source'],simulate:['steps','input','output','load-save','save-output','audio-output'],profile:['steps','input','output'],capture:['steps','input','ticks','output-dir','output','frames','width','height','no-overlay'],asset:['type','id','output','scale','seconds']};
 const args=[command];if(command==='asset'){if(!['schema','list','inspect','render','export','build'].includes(subcommand))throw Error('Unsupported asset command');args.push(subcommand);}
 if(!['capabilities','schema','script-api'].includes(command))args.push('--project',this.project.root);
 for(const [key,value]of Object.entries(options)){if(!allowed[command].includes(key))throw Error('Unsupported engine option: '+key);if(['output','save-output','audio-output','output-dir'].includes(key)){this.assertWrite();args.push('--'+key,await scoped(this.project.root,value,{write:true}));}else if(['input','load-save'].includes(key))args.push('--'+key,await scoped(this.project.root,value));else if(typeof value==='boolean'){if(value)args.push('--'+key);}else if(typeof value==='number'||typeof value==='string')args.push('--'+key,String(value));else throw Error('Engine options must be scalar');}
 // Asset compilation and captures write derived state, so they also honor the play lock.
 if(command==='capture'||command==='asset')this.assertWrite();const output=await execute(this.engine,args,{cwd:this.project.root,signal:this.signal,timeout:command==='capture'?180000:120000});let json;try{json=JSON.parse(output.stdout);}catch{}return{exit_code:output.code,...(output.code?{error:'Langmake command failed; inspect diagnostics and fix the project'}:{}),result:json??output.stdout.slice(0,100000),stderr:output.stderr.slice(0,12000)};
 }
 async runMusic(subcommand,options){
 const allowed={status:[],instruments:[],schema:[],validate:['score'],'pack-install':['directory'],'pack-verify':['pack'],render:['score','pack','output-dir','section','track'],audition:['instrument','pack','output-dir'],transform:['score','track','section','transpose','velocity','output'],import:['source','output-dir','frames','bpm','license']};
 if(!Object.hasOwn(allowed,subcommand))throw Error('Unsupported music command; sampler setup is a human installation step');
 const writing=['pack-install','render','audition','transform','import'].includes(subcommand);if(writing)this.assertWrite();
 const args=['music',subcommand];if(['render','audition','import'].includes(subcommand))args.push('--project',this.project.root);
 for(const [key,value] of Object.entries(options)){
  if(!allowed[subcommand].includes(key))throw Error('Unsupported music option: '+key);
  if(['directory','output-dir','output'].includes(key))args.push('--'+key,await scoped(this.project.root,value,{write:true}));
  else if(['score','pack','source'].includes(key))args.push('--'+key,await scoped(this.project.root,value));
  else if(typeof value==='number'||typeof value==='string')args.push('--'+key,String(value));
  else throw Error('Music options must be scalar');
 }
 const output=await execute(this.engine,args,{cwd:this.project.root,signal:this.signal,timeout:writing?600000:120000,env:{PYTHONDONTWRITEBYTECODE:'1'}});let json;try{json=JSON.parse(output.stdout);}catch{}
 return {exit_code:output.code,...(output.code?{error:'Music command failed; inspect the diagnostic'}:{}),result:json??output.stdout.slice(0,100000),stderr:output.stderr.slice(0,12000)};
 }

}
async function documentation(engine,name){if(!engine)throw Error('Choose an engine installation first');const parent=path.dirname(engine),candidates=[parent,path.join(parent,'docs'),path.resolve(parent,'../docs'),path.resolve(parent,'../../docs'),path.resolve(parent,'..'),path.resolve(parent,'../..')];const files=new Map();for(const root of candidates){try{for(const ent of await fs.readdir(root,{withFileTypes:true})){if(ent.isFile()&&(/^(UI-EDITOR|UI-EDITOR-QUALIFICATION|34-ui-editor-guide|35-ui-editor-qualification|MUSIC-AND-SOUND|AUDIO-QUALIFICATION|30-music-and-sound-guide|31-audio-qualification|AGENTS|AGENT-API|RUNTIME-GUIDE|TEXT-ASSETS|START-HERE|STUDIO|ANDROID|ANDROID-AGENT-GUIDE|ANDROID-QUALIFICATION|IOS|IOS-AGENT-GUIDE|IOS-QUALIFICATION|RELEASE-EVIDENCE|08-agent-api|13-runtime-guide|10-text-assets|17-studio|18-android|19-release-evidence|20-android-qualification|21-android-agent-guide|22-ios-plan|23-ios-agent-guide|24-ios-qualification|README)\.md$/i.test(ent.name)))files.set(ent.name,path.join(root,ent.name));}const android=path.join(root,'scripts/android/README.md');try{await fs.access(android);files.set('scripts/android/README.md',android);}catch{}const ios=path.join(root,'scripts/ios/README.md');try{await fs.access(ios);files.set('scripts/ios/README.md',ios);}catch{}const prompts=path.join(root,'prompts');for(const ent of await fs.readdir(prompts,{withFileTypes:true}).catch(()=>[]))if(ent.isFile()&&ent.name.endsWith('.md'))files.set('prompts/'+ent.name,path.join(prompts,ent.name));}catch{}}if(!name)return{documents:[...files.keys()]};if(!files.has(name))throw Error('Unknown installed document; list documentation first');const text=await fs.readFile(files.get(name),'utf8');if(text.length>300000)throw Error('Installed document exceeds300 KiB');return{path:name,content:text};}
module.exports={Tools,schemas,scoped,listFiles,readFile,writeFile,execute,kill,hash,within,documentation};
