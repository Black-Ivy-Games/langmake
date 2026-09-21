'use strict';
// Private stdio transport for a user-supplied Claude CLI. No tool execution occurs here.
const readline=require('node:readline');const url=process.env.LANGMAKE_BRIDGE_URL,token=process.env.LANGMAKE_BRIDGE_TOKEN;
if(!url?.startsWith('http://127.0.0.1:')||!token)throw Error('Studio bridge requires a private loopback session');
const lines=readline.createInterface({input:process.stdin});lines.on('line',async line=>{let m;try{if(line.length>3*1024*1024)throw Error('Request exceeds limit');m=JSON.parse(line);const r=await fetch(url,{method:'POST',headers:{Authorization:'Bearer '+token,'Content-Type':'application/json'},body:line});if(m.id!==undefined){const text=await r.text();if(!r.ok)throw Error(text);process.stdout.write(text+'\n');}}catch(e){if(m?.id!==undefined)process.stdout.write(JSON.stringify({jsonrpc:'2.0',id:m.id,error:{code:-32000,message:e.message}})+'\n');}});
