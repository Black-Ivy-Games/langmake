'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),path=require('node:path'),fs=require('node:fs/promises'),{randomUUID}=require('node:crypto');
const ios=require('../core/ios.cjs'),{Broker}=require('../core/broker.cjs'),{Tools}=require('../core/tools.cjs');
const {runtimeCandidates,executableFilters}=require('../core/host.cjs');
async function fixture(){const root=path.resolve(__dirname,'../.cache/ios-tests',randomUUID());await fs.mkdir(root,{recursive:true});await fs.writeFile(path.join(root,'project.json'),JSON.stringify({title:'Signal Kite',dimension:'2d'}));return root;}
const project=root=>({id:'1234-abcd',name:'Signal Kite',root,iosSettings:{bundleId:'org.example.kite',label:'Signal Kite',destination:'device',arch:'arm64',orientation:'portrait',configuration:'debug',teamId:'ABCDE12345',allowProvisioningUpdates:false}});
function staged(p){return{staged:path.join(ios.generatedRoot(p),'stage-fixture'),bundle_id:'org.example.kite',label:'Signal Kite',orientation:'portrait',configuration:'debug'};}
function built(p){return{app:path.join(ios.generatedRoot(p),'build-fixture','Signal.app'),bundle_id:'org.example.kite',label:'Signal Kite',orientation:'portrait',destination:'device',arch:'arm64',configuration:'debug'};}
test('source Studio can select extensionless Mac host tools without changing Windows pickers',()=>{
 const root=path.resolve('studio'),executable=path.resolve('studio/electron'),mac=runtimeCandidates(root,executable,{host:'darwin',override:'/explicit/langmake'});
 assert.equal(mac[0],'/explicit/langmake');assert(mac.slice(1).every(file=>path.basename(file)==='langmake'));assert.equal(executableFilters('Runtime','darwin'),undefined);
 assert.deepEqual(executableFilters('Runtime','win32'),[{name:'Runtime',extensions:['exe']}]);assert.equal(path.basename(runtimeCandidates(root,executable,{host:'win32',override:''})[0]),'langmake.exe');
});
test('iOS settings constrain identity, signing, destination and architecture',()=>{
 const p=project('S:\\game');assert.equal(ios.settings(p).teamId,'ABCDE12345');
 assert.equal(ios.settings(p).registerDevice,false);
 for(const value of [{bundleId:'bad id'},{destination:'ios'},{destination:'device',arch:'x86_64'},{orientation:'sideways'},{configuration:'production'},{teamId:'arbitrary shell value'},{allowProvisioningUpdates:'yes'},{registerDevice:'yes'},{registerDevice:true},{destination:'simulator',allowProvisioningUpdates:true},{destination:'simulator',registerDevice:true,allowProvisioningUpdates:true},{label:'line\nbreak'}])assert.throws(()=>ios.settings(p,value));
 assert.equal(ios.settings(p,{destination:'simulator',arch:'x86_64'}).arch,'x86_64');assert(!Object.hasOwn(ios.settings(p,{command:'unregistered'}),'command'));
});
test('Windows stages iOS content but does not pretend to build or install it',()=>{
 const p=project(path.resolve('studio/.cache/ios-game'));
 const args=ios.command('ios.py',p,'stage','',{engine:path.resolve('build/langmake.exe'),host:'win32'}),output=args[args.indexOf('--output')+1];
 assert.deepEqual(args.slice(0,12),['ios.py','stage','--project',p.root,'--bundle-id','org.example.kite','--label','Signal Kite','--orientation','portrait','--configuration','debug']);
 assert.equal(path.dirname(output),ios.generatedRoot(p));assert.notEqual(output,ios.command('ios.py',p,'stage','',{engine:path.resolve('build/langmake.exe'),host:'win32'}).at(-3));
 for(const action of ['build','devices','install','launch'])assert.throws(()=>ios.command('ios.py',p,action,'',{host:'win32'}),/Mac with Xcode/);
 assert.throws(()=>ios.command('ios.py',p,'unknown'),/Unsupported/);
});
test('Mac builds use staged content, explicit signing and opt-in provisioning',()=>{
 const p=project(path.resolve('studio/.cache/ios-game'));p.iosStage=staged(p);
 const args=ios.command('ios.py',p,'build','',{host:'darwin'});assert(args.includes('--team-id'));assert(!args.includes('--allow-provisioning-updates'));assert(!args.includes('--device'));assert(!args.includes('--register-device'));
 p.iosSettings.allowProvisioningUpdates=true;assert(ios.command('ios.py',p,'build','',{host:'darwin'}).includes('--allow-provisioning-updates'));
 p.iosSettings.orientation='landscape';assert.throws(()=>ios.command('ios.py',p,'build','',{host:'darwin'}),/Stage this project/);
});
test('physical builds pass only an explicitly selected eligible device and opt-in registration',()=>{
 const p=project(path.resolve('studio/.cache/ios-game'));p.iosStage=staged(p);const id='A101B202-C303-D404-E505-F60607080910';
 p.iosDevices=[{id,destination:'device',available:true,reality:'physical',reachable:true,pairing_state:'paired'}];
 let args=ios.command('ios.py',p,'build',id,{host:'darwin'});assert.equal(args[args.indexOf('--device')+1],id);assert(!args.includes('--register-device'));assert(!args.includes('--allow-provisioning-updates'));
 p.iosSettings.allowProvisioningUpdates=true;p.iosSettings.registerDevice=true;args=ios.command('ios.py',p,'build',id,{host:'darwin'});assert(args.includes('--allow-provisioning-updates'));assert(args.includes('--register-device'));
 assert.throws(()=>ios.command('ios.py',p,'build','',{host:'darwin'}),/Choose a physical iPhone/);
 assert.throws(()=>ios.command('ios.py',p,'build','unlisted',{host:'darwin'}),/Refresh Devices/);
 for(const invalid of [{available:false},{reality:'simulated'},{reality:'unknown'},{reachable:false},{pairing_state:'unpaired'},{exclusion_reason:'unsupported_platform'}]){const original=p.iosDevices[0];p.iosDevices=[{...original,...invalid}];assert.throws(()=>ios.command('ios.py',p,'build',id,{host:'darwin'}),/unavailable|paired physical/);p.iosDevices=[original];}
 p.iosDevices.push({...p.iosDevices[0]});assert.throws(()=>ios.command('ios.py',p,'build',id,{host:'darwin'}),/Refresh Devices/);
});
test('simulator builds remain generic and reject registration settings',()=>{
 const p=project(path.resolve('studio/.cache/ios-game'));p.iosStage=staged(p);Object.assign(p.iosSettings,{destination:'simulator',arch:'arm64'});p.iosDevices=[{id:'fixture-simulator',destination:'simulator',available:true,reality:'simulated',reachable:false}];
 const args=ios.command('ios.py',p,'build','fixture-simulator',{host:'darwin'});for(const flag of ['--device','--register-device','--allow-provisioning-updates','--team-id'])assert(!args.includes(flag));
 p.iosSettings.registerDevice=true;p.iosSettings.allowProvisioningUpdates=true;assert.throws(()=>ios.command('ios.py',p,'build','fixture-simulator',{host:'darwin'}),/only to physical/);
});
test('deployment rejects stale build identity, unlisted devices and artifact traversal',()=>{
 const p=project(path.resolve('studio/.cache/ios-game'));p.iosBuild=built(p);p.iosDevices=[{id:'00000000-0000000000000001',destination:'device'}];
 const args=ios.command('ios.py',p,'install',p.iosDevices[0].id,{host:'darwin'});assert.equal(args.at(-1),p.iosBuild.app);
 assert.throws(()=>ios.command('ios.py',p,'install','unlisted',{host:'darwin'}),/Refresh Devices/);
 assert.throws(()=>ios.contained(p,path.resolve(p.root,'outside.app')),/inside this project/);
 assert.throws(()=>ios.contained(p,path.join(ios.generatedRoot(p),'..','outside.app')),/inside this project/);
 p.iosSettings.bundleId='org.example.other';assert.throws(()=>ios.command('ios.py',p,'launch',p.iosDevices[0].id,{host:'darwin'}),/Build this project/);
});
test('device stop uses the launched process identity and does not guess',()=>{
 const p=project(path.resolve('studio/.cache/ios-game')),player={ios:true,destination:'device',device:'fixture-device',bundleId:'org.example.kite',pid:4321};
 assert.deepEqual(ios.command('ios.py',p,'stop','',{host:'darwin',player}),['ios.py','stop','--destination','device','--device','fixture-device','--bundle-id','org.example.kite','--pid','4321']);
 assert.throws(()=>ios.command('ios.py',p,'stop','',{host:'darwin',player:{...player,pid:undefined}}),/process ID/);
});
test('generated iOS output rejects junctions before creating build files',async()=>{
 const root=await fixture(),outside=await fixture(),p=project(root);await fs.symlink(outside,path.join(root,'.langmake'),'junction');await assert.rejects(ios.prepareRoot(p),/links|junctions/);assert.deepEqual((await fs.readdir(outside)).sort(),['project.json']);
});
test('artifact verification rejects redirected build reports',async()=>{
 const p=project(await fixture()),outside=await fixture();await ios.prepareRoot(p);const fake=path.join(ios.generatedRoot(p),'build-link');await fs.symlink(outside,fake,'junction');await assert.rejects(ios.inspectArtifact(p,path.join(fake,'project.json')),/links|junctions/);
});
for(const layout of ['source','portable'])test(`iOS agent documentation is discoverable in ${layout} installations`,async()=>{
 const root=await fixture(),engine=path.join(root,layout==='source'?'build':'runtime','langmake.exe'),docs=layout==='source'?path.join(root,'docs'):path.dirname(engine);await fs.mkdir(path.dirname(engine),{recursive:true});await fs.mkdir(docs,{recursive:true});
 const names=layout==='source'?['23-ios-agent-guide.md','24-ios-qualification.md']:['IOS-AGENT-GUIDE.md','IOS-QUALIFICATION.md','IOS.md'];for(const name of names)await fs.writeFile(path.join(docs,name),'iOS host checks are not device qualification.');
 const tools=new Tools({project:{root:await fixture()},engine,signal:new AbortController().signal}),index=await tools.call('documentation');for(const name of names){assert(index.documents.includes(name));assert.match((await tools.call('documentation',{path:name})).content,/not device qualification/);}
});
test('broker protects human play and stores only matching successful iOS artifacts',async()=>{
 const calls=[],fakeExecutor=async(_python,args)=>{calls.push(args);const action=args[1],arg=key=>args[args.indexOf(key)+1];let result={ok:true};if(action==='stage'){await fs.mkdir(arg('--output'));result={ok:true,staged:arg('--output'),bundle_id:arg('--bundle-id'),orientation:arg('--orientation'),configuration:arg('--configuration')};}if(action==='build'){const app=path.join(arg('--output'),'Signal.app');await fs.mkdir(app,{recursive:true});result={ok:true,app,bundle_id:'org.example.kite',destination:'device',arch:'arm64',configuration:'debug'};}if(action==='devices')result={ok:true,devices:[{id:'fixture-device',name:'Explicit test target',available:true}]};if(action==='launch')result={ok:true,pid:1234};return{code:0,stdout:JSON.stringify(result),stderr:''};};
 const b=await new Broker({dataRoot:await fixture(),host:'darwin',targetExecutor:fakeExecutor}).init(),p=await b.addProject(await fixture());p.iosSettings=project(p.root).iosSettings;b.store.data.engine=path.resolve('build/langmake.exe');b.store.data.ios={python:'python',script:path.resolve('scripts/ios/ios.py')};
 await b.ios(p.id,'stage');await b.ios(p.id,'build');await b.ios(p.id,'devices');await b.ios(p.id,'install','fixture-device');await b.ios(p.id,'launch','fixture-device');assert(b.playing.has(p.id));await assert.rejects(b.edit(p.id,'change.txt','bad',null),/human is playing/);await assert.rejects(b.iosSettings(p.id,{label:'Changed'}),/human is playing/);await b.stop(p.id);assert(!b.playing.has(p.id));assert.equal(calls.at(-1).at(-1),'1234');assert.equal(b.running.size,0);
 assert(b.context(p,{messages:[]}).includes('23-ios-agent-guide.md'));assert(b.context(p,{messages:[]}).includes('Do not claim an iPhone build'));
 b.targetExecutor=async()=>({code:1,stdout:JSON.stringify({ok:true,staged:'bogus'}),stderr:'failed'});const previous=p.iosStage;assert.equal((await b.ios(p.id,'stage')).ok,false);assert.equal(p.iosStage,previous);
});
test('a mismatched successful build report is not accepted and releases the writer',async()=>{
 const b=await new Broker({dataRoot:await fixture(),host:'darwin',targetExecutor:async()=>({code:0,stdout:JSON.stringify({ok:true,app:'/outside/Other.app',bundle_id:'other',destination:'device',arch:'arm64',configuration:'debug'}),stderr:''})}).init(),p=await b.addProject(await fixture());p.iosSettings=project(p.root).iosSettings;p.iosStage=staged(p);await fs.mkdir(p.iosStage.staged,{recursive:true});b.store.data.ios={python:'python',script:path.resolve('scripts/ios/ios.py')};await assert.rejects(b.ios(p.id,'build'),/does not match/);assert.equal(p.iosBuild,undefined);assert.equal(b.running.size,0);
});
test('broker persists registration opt-in and normalized device eligibility without selecting a default',async()=>{
 const record={id:'fixture-physical',name:'Explicit fixture iPhone',available:true,reality:'physical',reachable:true,pairing_state:'paired',developer_mode:'enabled',exclusion_reason:null};
 const b=await new Broker({dataRoot:await fixture(),host:'darwin',targetExecutor:async()=>({code:0,stdout:JSON.stringify({ok:true,devices:[record,{...record,id:'disconnected',available:false}]}),stderr:''})}).init(),p=await b.addProject(await fixture());
 b.store.data.ios={python:'python',script:path.resolve('scripts/ios/ios.py')};
 const target=await b.iosSettings(p.id,{...project(p.root).iosSettings,allowProvisioningUpdates:true,registerDevice:true});assert.equal(target.registerDevice,true);
 await b.ios(p.id,'devices');assert.equal(p.iosDevices.length,1);for(const key of ['available','reality','reachable','pairing_state','developer_mode','exclusion_reason'])assert.equal(p.iosDevices[0][key],record[key]);
 assert.equal(b.store.data.projects.find(item=>item.id===p.id).iosSettings.registerDevice,true);assert.equal(p.iosDevice,undefined);
 const persisted=JSON.parse(await fs.readFile(path.join(b.store.root,'studio.json'),'utf8')).projects.find(item=>item.id===p.id);assert.equal(persisted.iosSettings.registerDevice,true);assert.equal(persisted.iosDevices[0].reality,'physical');
});
