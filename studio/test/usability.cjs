'use strict';
// Real Electron + native project creation, with a deliberately fake API.
const {_electron:electron}=require('playwright-core'),fs=require('node:fs/promises'),path=require('node:path'),http=require('node:http'),assert=require('node:assert/strict');
const {Store}=require('../core/store.cjs');
(async()=>{
 const base=path.resolve('studio/.cache/usability-'+Date.now()),data=path.join(base,'data'),packageRoot=process.env.LANGMAKE_STUDIO_PACKAGE;
 const engine=packageRoot?path.join(packageRoot,'runtime/langmake.exe'):path.resolve('build/langmake.exe');await fs.mkdir(base,{recursive:true});
 let round=0,empty=false;const server=http.createServer(async(req,res)=>{
  let body='';for await(const chunk of req)body+=chunk;const input=JSON.parse(body);round++;
  res.setHeader('Content-Type','text/event-stream');
  if(!empty&&round===1)res.write('data: '+JSON.stringify({choices:[{delta:{tool_calls:[{index:0,id:'read',function:{name:'list_files',arguments:'{}'}}]}}]})+'\n\n');
  if(!empty&&round===3){assert.equal(input.tools,undefined);res.write('data: '+JSON.stringify({choices:[{delta:{content:'I can read you. The connection works.'}}]})+'\n\n');}
  res.end('data: [DONE]');
 });await new Promise(r=>server.listen(0,'127.0.0.1',r));
 const store=await new Store(data).init();store.data.engine=engine;store.data.profiles=[{id:'fixture',name:'Fake local model',type:'local',model:'fixture',endpoint:'http://127.0.0.1:'+server.address().port+'/v1'}];await store.save();
 const env={...process.env,LANGMAKE_STUDIO_DATA:data,LANGMAKE_ENGINE:engine,LANGMAKE_STUDIO_TEST:'1'};delete env.ELECTRON_RUN_AS_NODE;let app;
 try{
  app=await electron.launch({...(packageRoot?{executablePath:path.join(packageRoot,'Langmake Studio.exe'),args:[]}:{args:[path.resolve('studio')]}),env});const page=await app.firstWindow();await page.waitForSelector('#newProject');
  await app.evaluate(({dialog},base)=>{dialog.showOpenDialog=async()=>({canceled:false,filePaths:[base]});},base);
  await page.locator('#newProject').click();await page.locator('#newName').fill('Qwen Connection Test');await page.locator('#template').selectOption('workflow-2d');await page.locator('#newForm button[type=submit]').click();
  await page.waitForFunction(()=>document.getElementById('title').textContent==='Qwen Connection Test');
  const game=path.join(base,'Qwen Connection Test');assert.equal(JSON.parse(await fs.readFile(path.join(game,'project.json'),'utf8')).title,'Glow Garden');
  await page.locator('#prompt').fill('How do you read me?');await page.locator('#send').click();await page.getByText('I can read you. The connection works.',{exact:true}).waitFor();await page.waitForFunction(()=>!document.getElementById('send').disabled);
  assert.equal(round,3);assert.equal(await page.locator('#activity').textContent(),'Response complete');assert.equal(await page.locator('.message.assistant').count(),1);assert.equal(await page.locator('.activity-group').count(),1);assert.equal(await page.locator('.activity-group').getAttribute('open'),null);
  await page.screenshot({path:path.join(base,'answer.png')});
  empty=true;round=0;await page.locator('#prompt').fill('Empty fixture response');await page.locator('#send').click();await page.getByText('The model returned no visible answer, even after a final-answer retry. Check its chat template, reasoning settings and tool support.',{exact:true}).waitFor();await page.waitForFunction(()=>!document.getElementById('send').disabled);assert.equal(round,2);
  await page.reload();await page.waitForFunction(()=>document.getElementById('title').textContent==='Qwen Connection Test');assert(await page.locator('#openLayout').isEnabled());
  await page.locator('#settings').click();await page.getByText(/Studio build: 2026-09-22/).waitFor();await page.screenshot({path:path.join(base,'settings.png')});
  const saved=JSON.parse(await fs.readFile(path.join(data,'studio.json'),'utf8'));assert.equal(saved.chats[0].status,'error');assert.equal(saved.projects[0].name,'Qwen Connection Test');
  await fs.writeFile(path.join(base,'evidence.json'),JSON.stringify({ok:true,packageRoot:packageRoot||null,adapter:'explicit fake loopback API',checks:['New keeps entered Studio name and original authored title','name persists on reload','tool call followed by empty completion recovers one final answer','activity collapsed','repeated empty completion visibly errors','UI editor available without running an agent','build identity visible']},null,2));console.log(JSON.stringify({ok:true,evidence:base}));
 }finally{await app?.close();server.closeAllConnections();server.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
