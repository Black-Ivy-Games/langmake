'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const id=()=>crypto.randomUUID();
async function atomic(file,value){await fs.mkdir(path.dirname(file),{recursive:true});const tmp=file+'.'+id()+'.tmp';await fs.writeFile(tmp,JSON.stringify(value,null,2),'utf8');try{await fs.rename(tmp,file);}catch(e){await fs.rm(tmp,{force:true});throw e;}}
class Store{
 constructor(root){this.root=path.resolve(root);this.file=path.join(root,'studio.json');this.data={version:1,engine:'',projects:[],profiles:[],chats:[]};this.chain=Promise.resolve();}
 async init(){await fs.mkdir(this.root,{recursive:true});try{this.data=JSON.parse(await fs.readFile(this.file,'utf8'));if(this.data.version!==1)throw Error('Unsupported Studio data version');}catch(e){if(e.code!=='ENOENT')throw e;}for(const c of this.data.chats)if(c.status==='running'){c.status='interrupted';c.messages.push({role:'system',text:'Studio closed during this run. Inspect its checkpoint before continuing.',at:Date.now()});}await this.save();return this;}
 save(){const value=JSON.parse(JSON.stringify(this.data));this.chain=this.chain.catch(()=>{}).then(()=>atomic(this.file,value));return this.chain;}
 project(projectId){const p=this.data.projects.find(p=>p.id===projectId);if(!p)throw Error('Unknown project');return p;}
 chat(chatId){const c=this.data.chats.find(c=>c.id===chatId);if(!c)throw Error('Unknown chat');return c;}
 publicState(){return {...this.data,profiles:this.data.profiles.map(({secret,...p})=>({...p,hasSecret:!!secret})),chats:this.data.chats.map(c=>({...c,messages:c.messages.slice(-250)}))};}
}
module.exports={Store,atomic,id};
