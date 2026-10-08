'use strict';
// Optional authoring service. The native runtime owns layout and pixels.
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto'),{spawn}=require('node:child_process');
const digest=b=>crypto.createHash('sha256').update(b).digest('hex');
const props=['position','left','top','right','bottom','width','height','margin-left','margin-top','margin-right','margin-bottom','padding-left','padding-top','padding-right','padding-bottom','font-size','color','background-color','opacity','z-index','text-align','display','border-radius'];
async function scoped(root,relative,write=false){
 if(typeof relative!=='string'||!relative||relative.length>512||path.isAbsolute(relative)||/[:\0]/.test(relative))throw Error('Expected a project-relative path');
 const parts=relative.replaceAll('\\','/').split('/');if(parts.some(p=>p==='..'||['.git','.studio','node_modules'].includes(p.toLowerCase())))throw Error('Path crosses a protected boundary');
 let current=await fs.realpath(root);for(const part of parts.filter(p=>p&&p!=='.')){current=path.join(current,part);try{if((await fs.lstat(current)).isSymbolicLink())throw Error('Linked paths are not supported');}catch(e){if(e.code==='ENOENT'&&write)continue;throw e;}}
 return current;
}
async function text(file,limit=1024*1024){if((await fs.stat(file)).size>limit)throw Error('Editor text limit is 1 MiB');return fs.readFile(file,'utf8');}
async function atomic(file,data){const tmp=file+'.'+crypto.randomUUID()+'.tmp';await fs.mkdir(path.dirname(file),{recursive:true});await fs.writeFile(tmp,data,{flag:'wx'});try{await fs.rename(tmp,file);}finally{await fs.rm(tmp,{force:true});}}
const decode=s=>s.replace(/&quot;/g,'"').replace(/&apos;/g,"'").replace(/&lt;/g,'<').replace(/&gt;/g,'>').replace(/&amp;/g,'&');
const encode=s=>s.replaceAll('&','&amp;').replaceAll('"','&quot;').replaceAll('<','&lt;').replaceAll('>','&gt;');
function tags(source){
 const out=[];const re=/<(?:!--[\s\S]*?--|[^>"']|"[^"]*"|'[^']*')*>/g;let m;
 while((m=re.exec(source))){if(/^<[/!?]/.test(m[0]))continue;const attrs=Object.create(null);for(const a of m[0].matchAll(/\s([\w:-]+)\s*=\s*("([^"]*)"|'([^']*)')/g)){if(Object.hasOwn(attrs,a[1]))throw Error('Duplicate RML attribute');attrs[a[1]]={value:decode(a[3]??a[4]),start:a.index,end:a.index+a[0].length};}if(attrs.id)out.push({start:m.index,end:re.lastIndex,raw:m[0],id:attrs.id.value,attrs});}
 return out;
}
function styles(tag){const entries=(tag.attrs.style?.value||'').split(';').filter(s=>s.trim());const result=Object.create(null);for(const entry of entries){const i=entry.indexOf(':');if(i<1||/[()'"{}]/.test(entry))throw Error('Complex inline styles require source editing; no changes were made');result[entry.slice(0,i).trim()]=entry.slice(i+1).trim();}return result;}
function property(key,value){
 if(!props.includes(key))throw Error('Unsupported editor property: '+key);if(value===null||value==='')return null;
 if(typeof value!=='string'||value.length>80)throw Error('Property values must be short CSS strings');value=value.trim();let ok=false;
 if(key==='position')ok=['absolute','relative','static'].includes(value);
 else if(key==='display')ok=['block','inline','inline-block','flex','none'].includes(value);
 else if(key==='text-align')ok=['left','center','right'].includes(value);
 else if(key==='color'||key==='background-color')ok=/^(#(?:[a-f\d]{3}|[a-f\d]{4}|[a-f\d]{6}|[a-f\d]{8})|transparent|white|black|red|blue|green)$/i.test(value);
 else if(key==='opacity')ok=/^(0(\.\d+)?|1(\.0+)?)$/.test(value);
 else if(key==='z-index')ok=/^-?\d{1,4}$/.test(value)||value==='auto';
 else ok=(value==='auto'&&['left','right','top','bottom','width','height'].includes(key))||/^-?\d{1,4}(\.\d{1,3})?(dp|px|%)$/.test(value);
 if(!ok)throw Error('Unsupported value for '+key+': '+value);if(/^(width|height|font-size|padding|border)/.test(key)&&value.startsWith('-'))throw Error('Size/padding cannot be negative');return value;
}
function patch(source,id,changes){
 if(typeof id!=='string'||id.length>128)throw Error('Invalid element id');if(!changes||Array.isArray(changes)||typeof changes!=='object'||Object.keys(changes).length>props.length)throw Error('Expected a bounded property map');
 const matches=tags(source).filter(t=>t.id===id);if(matches.length!==1)throw Error('Element needs a unique quoted id in this document');const tag=matches[0],values=styles(tag);
 for(const [k,v] of Object.entries(changes)){const checked=property(k,v);if(checked===null)delete values[k];else values[k]=checked;}
 const style=Object.entries(values).map(([k,v])=>k+': '+v).join('; ');let opening=tag.raw;
 if(tag.attrs.style){const a=tag.attrs.style;opening=opening.slice(0,a.start)+(style?' style="'+encode(style)+'"':'')+opening.slice(a.end);}
 else if(style)opening=opening.replace(/(\/?\s*>)$/,' style="'+encode(style)+'"$1');
 return source.slice(0,tag.start)+opening+source.slice(tag.end);
}
function run(engine,args,cwd){return new Promise((resolve,reject)=>{const child=spawn(engine,args,{cwd,windowsHide:true,shell:false,stdio:['ignore','pipe','pipe']});let stdout='',stderr='',done=false;const timer=setTimeout(()=>{child.kill();finish(Error('Native UI operation timed out'));},120000);function finish(err,value){if(done)return;done=true;clearTimeout(timer);err?reject(err):resolve(value);}child.on('error',finish);child.stdout.on('data',b=>{stdout+=b;if(stdout.length>16*1024*1024){child.kill();finish(Error('Native response exceeds budget'));}});child.stderr.on('data',b=>{stderr=(stderr+b).slice(-16000);});child.on('close',code=>{let value;try{value=JSON.parse(stdout);}catch{return finish(Error('Native response was not JSON: '+stderr));}if(code||!value.ok)return finish(Error(JSON.stringify(value.diagnostics||value)+' '+stderr));finish(null,value);});});}
function config(value={}){
 const c={width:1280,height:720,density:1,tick:0,profile:'',input:'',safe_area:'',...value};
 if(Object.keys(c).some(k=>!['width','height','density','tick','profile','input','safe_area'].includes(k)))throw Error('Unknown preview setting');
 for(const key of ['width','height'])if(!Number.isInteger(c[key])||c[key]<128||c[key]>2560)throw Error('Preview dimensions must be 128..2560 physical pixels');
 if(!Number.isFinite(c.density)||c.density<.25||c.density>8||!Number.isInteger(c.tick)||c.tick<0||c.tick>3600)throw Error('Invalid density or tick (0..3600)');
 if(typeof c.profile!=='string'||c.profile.length>64||typeof c.input!=='string'||typeof c.safe_area!=='string')throw Error('Invalid preview strings');
 if(c.safe_area&&!/^\d+,\d+,\d+,\d+$/.test(c.safe_area))throw Error('Safe area is x,y,width,height');return c;
}
async function operate({root,engine,action,session,version,...request}){
 root=await fs.realpath(root);engine=path.resolve(engine);const base=await scoped(root,'.langmake/ui-editor',true);await fs.mkdir(base,{recursive:true});
 const lock=path.join(base,'operation.lock');let handle;try{handle=await fs.open(lock,'wx');}catch(e){if(e.code==='EEXIST')throw Error('Another UI editor operation is active. If its process crashed, remove .langmake/ui-editor/operation.lock after verifying it has stopped.');throw e;}
 try{return await operation();}finally{await handle.close();await fs.rm(lock,{force:true});}
 async function operation(){
  if(action==='discover'){const drafts=[];for(const e of await fs.readdir(base,{withFileTypes:true})){if(!e.isDirectory()||!/^[-a-f\d]{36}$/.test(e.name))continue;try{const s=JSON.parse(await text(await scoped(root,'.langmake/ui-editor/'+e.name+'/session.json'),40*1024*1024));drafts.push({session:e.name,document:s.document,applied:!!s.applied});}catch{}}const project=JSON.parse(await text(await scoped(root,'project.json')));return {ok:true,drafts,profiles:Object.keys(project.profiles||{}),documents:[...new Set([...(Object.values(project.ui?.documents||{})),...Object.values(project.profiles||{}).flatMap(p=>Object.values(p.ui?.documents||{}))])],scenarios:project.scenarios||{}};}
  if(action==='open'){
   const dirs=(await fs.readdir(base,{withFileTypes:true})).filter(d=>d.isDirectory());if(dirs.length>=8)throw Error('Eight drafts already exist; discard an old draft before opening another');
   const c=config(request.config),before=await run(engine,['validate','--project',root],root);session=crypto.randomUUID();const folder=await scoped(root,'.langmake/ui-editor/'+session,true);await fs.mkdir(folder);
   try{
    const args=['cook','--project',root,'--target','windows','--output',path.join(folder,'game')];if(c.profile)args.push('--profile',c.profile);await run(engine,args,root);
    const after=await run(engine,['validate','--project',root],root);if(before.revision!==after.revision)throw Error('Source changed during draft creation; reopen');
    const manifest=JSON.parse(await text(path.join(folder,'game/cook-manifest.json')));const documents=manifest.files.map(f=>f.path).filter(f=>f.endsWith('.rml'));
    const cookedProject=JSON.parse(await text(path.join(folder,'game/project.json'))),active=Object.values(cookedProject.ui?.documents||{});const document=request.document||active[0];if(!documents.includes(document)||!active.includes(document))throw Error('Choose a document active in the selected profile (or use the default document)');
    const original=await text(await scoped(root,document));if(await text(await scoped(path.join(folder,'game'),document))!==original)throw Error('Cooked document differs from source');
    if(c.input){const input=JSON.parse(await text(await scoped(root,c.input)));await atomic(path.join(folder,'input.json'),JSON.stringify(input.input||input));}
    const s={format:1,id:session,version:0,document,documents,config:c,base_revision:before.revision,base_hash:digest(original),original,history:[original],cursor:0,notes:Object.create(null),applied:false};await save(folder,s);return await capture(folder,s);
   }catch(e){await fs.rm(folder,{recursive:true,force:true});throw e;}
  }
  if(!/^[a-f\d-]{36}$/.test(session||''))throw Error('Invalid draft id');const folder=await scoped(root,'.langmake/ui-editor/'+session);const s=JSON.parse(await text(path.join(folder,'session.json'),40*1024*1024));
  if(s.id!==session||s.format!==1||!Number.isInteger(s.version)||!Array.isArray(s.history)||s.history.length>31||!s.history.every(t=>typeof t==='string'&&Buffer.byteLength(t)<=1024*1024)||!Number.isInteger(s.cursor)||!s.history[s.cursor]||typeof s.document!=='string'||!s.document.endsWith('.rml'))throw Error('Invalid stored draft');
  await scoped(root,s.document);await scoped(root,'.langmake/ui-editor/'+session+'/game/project.json');config(s.config);if(s.capture&&(!/^capture-[a-f\d-]{36}\/tick-\d+\.png$/.test(s.capture.image)||s.capture.metadata!==s.capture.image+'.json'))throw Error('Invalid stored capture');
  s.notes=Object.assign(Object.create(null),s.notes);
  if(action==='read')return view(folder,s);
  if(version!==s.version)throw Error('Draft revision changed; read it again');
  if(action==='discard'){await fs.rm(folder,{recursive:true,force:true});return {ok:true,discarded:true};}
  if(action==='capture')return capture(folder,s);
  if(action==='patch'||action==='undo'||action==='redo'){
   if(s.applied)throw Error('Draft was applied; open a new draft to edit again');const prior=s.history[s.cursor];let next;
   if(action==='patch'){next=patch(prior,request.id,request.properties);if(s.history.length>=31)throw Error('Draft history limit reached; apply or discard before more edits');}
   else{const cursor=s.cursor+(action==='undo'?-1:1);if(cursor<0||cursor>=s.history.length)throw Error('No '+action+' available');next=s.history[cursor];}
   const file=await scoped(path.join(folder,'game'),s.document);await atomic(file,next);
   try{await run(engine,['ui-check','--project',path.join(folder,'game'),...viewport(s.config)],root);}catch(e){await atomic(file,prior);throw e;}
   if(action==='patch'){s.history=s.history.slice(0,s.cursor+1);s.history.push(next);s.cursor++;}else s.cursor+=action==='undo'?-1:1;
   s.version++;await save(folder,s);return capture(folder,s);
  }
  if(action==='note'){
   if(!tags(s.history[s.cursor]).some(t=>t.id===request.id)||typeof request.text!=='string'||request.text.length>2000||typeof request.locked!=='boolean')throw Error('Notes require a document element, text up to 2000 characters and a lock boolean');
   if(!Object.hasOwn(s.notes,request.id)&&Object.keys(s.notes).length>=128)throw Error('Review note limit is 128 elements');s.notes[request.id]={text:request.text,locked:request.locked};s.version++;await save(folder,s);return view(folder,s);
  }
  if(action==='apply'){
   if(s.applied)throw Error('Draft already applied');await run(engine,['validate','--project',path.join(folder,'game')],root);
   const current=await run(engine,['validate','--project',root],root);if(current.revision!==s.base_revision)throw Error('Project source changed since the draft opened; reopen rather than overwrite it');
   const target=await scoped(root,s.document);if(digest(await text(target))!==s.base_hash)throw Error('Document revision conflict');
   await atomic(target,s.history[s.cursor]);s.applied=true;s.version++;await save(folder,s);return view(folder,s);
  }
  if(action==='export'){
   if(!s.capture||s.capture.cursor!==s.cursor)throw Error('Refresh the native preview before exporting');
   const name='ui-design-reviews/'+s.id+'-'+s.version;const output=await scoped(root,name,true);await fs.mkdir(output,{recursive:true});
   const sidecar=JSON.parse(await text(await scoped(folder,s.capture.metadata),16*1024*1024));await atomic(await scoped(output,'preview.png',true),await fs.readFile(await scoped(folder,s.capture.image)));
   const report={format_version:1,kind:'human-ui-direction',document:s.document,source_revision:s.base_revision,draft_revision:sidecar.source_revision||sidecar.revision,applied:s.applied,preview:s.config,screenshot:name+'/preview.png',notes:s.notes,design_locks_are:'advisory human instructions; later human direction takes precedence',elements:sidecar.ui_renderer.elements,original_sha256:s.base_hash,desired_sha256:digest(s.history[s.cursor]),desired_document:name+'/desired.rml',native_sidecar:name+'/preview.json'};
   await atomic(path.join(output,'desired.rml'),s.history[s.cursor]);await atomic(path.join(output,'preview.json'),JSON.stringify(sidecar,null,2));await atomic(path.join(output,'review.json'),JSON.stringify(report,null,2));
   // Unique reviews preserve previous directions; a stable pointer is easy for agents to discover.
   const brief=await scoped(root,'ui-design.json',true);await atomic(brief,JSON.stringify({...report,review:name+'/review.json'},null,2));return {ok:true,review:name+'/review.json',brief:'ui-design.json',screenshot:report.screenshot};
  }
  throw Error('Unknown UI editor action');
 }
 function viewport(c){const args=['--width',String(c.width),'--height',String(c.height),'--density',String(c.density)];if(c.safe_area)args.push('--safe-area',c.safe_area);return args;}
 async function save(folder,s){await atomic(path.join(folder,'session.json'),JSON.stringify(s));}
 async function capture(folder,s){
  const name='capture-'+crypto.randomUUID(),out=path.join(folder,name);const args=['capture','--project',path.join(folder,'game'),'--steps',String(s.config.tick),'--ticks',String(s.config.tick),'--output-dir',out,'--no-overlay','--no-audio',...viewport(s.config)];if(s.config.input)args.push('--input',path.join(folder,'input.json'));
  await run(engine,args,root);const image=name+'/tick-'+s.config.tick+'.png';const metadata=image+'.json';const data=JSON.parse(await text(await scoped(folder,metadata),16*1024*1024));if(data.ui_renderer?.editor_revision!==1)throw Error('Runtime lacks UI editor observations; rebuild/use the matching runtime');
  const old=s.capture;s.capture={image,metadata,cursor:s.cursor};s.version++;await save(folder,s);if(old)await fs.rm(path.join(folder,old.image.split('/')[0]),{recursive:true,force:true});return view(folder,s);
 }
 async function view(folder,s){const content=s.history[s.cursor],elements=tags(content).map(t=>{try{return {id:t.id,inline:styles(t),editable:true};}catch(e){return {id:t.id,inline:{},editable:false,diagnostic:e.message};}});let capture=null;
  if(s.capture){const data=JSON.parse(await text(await scoped(folder,s.capture.metadata),16*1024*1024));capture={image:'data:image/png;base64,'+(await fs.readFile(await scoped(folder,s.capture.image))).toString('base64'),state:data,stale:s.capture.cursor!==s.cursor};}
  return {ok:true,session:s.id,version:s.version,document:s.document,documents:s.documents,config:s.config,original:s.original,content,elements,notes:s.notes,applied:s.applied,dirty:content!==s.original,canUndo:s.cursor>0&&!s.applied,canRedo:s.cursor+1<s.history.length&&!s.applied,capture,properties:props};
 }
}
module.exports={operate,patch,property,tags,scoped,config};
