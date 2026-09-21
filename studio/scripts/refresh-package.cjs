'use strict';
// Refresh an existing, unlaunched distribution from canonical Studio application
// sources. Dependencies/runtime are unchanged; use package.cjs for those changes.
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const root=path.resolve(__dirname,'..'),workspace=path.dirname(root);
const inside=(parent,child)=>{const relative=path.relative(parent,child);return relative!==''&&!relative.startsWith('..'+path.sep)&&relative!=='..'&&!path.isAbsolute(relative);};
async function list(dir,base=dir){const result=[];for(const entry of await fs.readdir(dir,{withFileTypes:true})){const file=path.join(dir,entry.name);if(entry.isSymbolicLink())throw Error('Distribution must not contain links');if(entry.isDirectory()){if(['.data','.cache'].includes(entry.name))throw Error('Refresh a clean distribution, not a launched copy');result.push(...await list(file,base));}else if(entry.isFile())result.push({file,path:path.relative(base,file).split(path.sep).join('/')});}return result;}
(async()=>{
 const index=process.argv.indexOf('--package');if(index<0||!process.argv[index+1])throw Error('Usage: node scripts/refresh-package.cjs --package <workspace distribution>');
 const folder=await fs.realpath(path.resolve(process.argv[index+1]));if(!inside(path.join(workspace,'dist'),folder)&&!inside(path.join(root,'dist'),folder))throw Error('Distribution must be inside workspace dist or studio/dist');
 await fs.access(path.join(folder,'Langmake Studio.exe'));const manifestPath=path.join(folder,'studio-package-manifest.json'),manifest=JSON.parse(await fs.readFile(manifestPath,'utf8'));await list(folder);
 const allowedTop=new Set(['.gitignore','LICENSE','main.cjs','preload.cjs','package.json','README.md','THIRD_PARTY_NOTICES.md']),allowedDirs=new Set(['core','providers','renderer']),changed=[];
 for(const entry of manifest.files){if(!entry.path.startsWith('resources/app/'))continue;const relative=entry.path.slice('resources/app/'.length),parts=relative.split('/');if(!(parts.length===1?allowedTop.has(relative):allowedDirs.has(parts[0])))continue;
  const source=await fs.realpath(path.join(root,relative)),target=await fs.realpath(path.join(folder,entry.path));if(!inside(root,source)||!inside(folder,target))throw Error('Application path escapes distribution');
  const bytes=await fs.readFile(source);if(!bytes.equals(await fs.readFile(target))){await fs.copyFile(source,target);changed.push(entry.path);}
 }
 for(const [from,to] of [['README.md','START-HERE.md'],['THIRD_PARTY_NOTICES.md','STUDIO-NOTICES.md']])await fs.copyFile(path.join(root,from),path.join(folder,to));
 const files=[];for(const item of await list(folder)){if(item.path==='studio-package-manifest.json')continue;const bytes=await fs.readFile(item.file);files.push({path:item.path,bytes:bytes.length,sha256:crypto.createHash('sha256').update(bytes).digest('hex')});}
 await fs.writeFile(manifestPath,JSON.stringify({...manifest,files},null,2));console.log(JSON.stringify({ok:true,folder,changed,files:files.length}));
})().catch(error=>{console.error(error);process.exitCode=1;});
