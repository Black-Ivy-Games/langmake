'use strict';
const path=require('node:path'),fs=require('node:fs/promises'),{randomUUID}=require('node:crypto');
const ACTIONS=['doctor','devices','stage','build','install','launch','stop'];
function settings(project,changes={}){
 const source={bundleId:'org.langmake.game.g'+project.id.replace(/[^a-z0-9]/gi,'').toLowerCase(),label:project.name,destination:'device',arch:'arm64',orientation:'landscape',configuration:'debug',teamId:'',allowProvisioningUpdates:false,registerDevice:false,...project.iosSettings,...changes};
 const result=Object.fromEntries(['bundleId','label','destination','arch','orientation','configuration','teamId','allowProvisioningUpdates','registerDevice'].map(key=>[key,source[key]]));
 if(typeof result.bundleId!=='string'||result.bundleId.length>150||!/^([A-Za-z][A-Za-z0-9-]*\.){2,}[A-Za-z][A-Za-z0-9-]*$/.test(result.bundleId))throw Error('iOS bundle ID needs at least three dotted segments starting with letters');
 if(typeof result.label!=='string'||!result.label.trim()||result.label.length>80||/[\x00-\x1f]/.test(result.label))throw Error('iOS display label must contain 1..80 printable characters');
 if(!['device','simulator'].includes(result.destination))throw Error('Choose an iPhone device or iOS simulator');
 if(!['arm64','x86_64'].includes(result.arch)||(result.destination==='device'&&result.arch!=='arm64'))throw Error('iPhones require arm64; x86_64 is for Intel Mac simulators');
 if(!['landscape','portrait','sensor'].includes(result.orientation))throw Error('Choose landscape, portrait or automatic rotation');
 if(!['debug','release'].includes(result.configuration))throw Error('Choose a development or release player');
 if(typeof result.teamId!=='string'||(result.teamId&&!/^[A-Z0-9]{10}$/.test(result.teamId)))throw Error('Apple signing team must be the 10-character Team ID shown in Xcode');
 if(typeof result.allowProvisioningUpdates!=='boolean')throw Error('Provisioning updates must be explicitly enabled or disabled');
 if(typeof result.registerDevice!=='boolean')throw Error('Device registration must be explicitly enabled or disabled');
 if(result.destination!=='device'&&(result.allowProvisioningUpdates||result.registerDevice))throw Error('Provisioning updates and device registration apply only to physical iPhone builds');
 if(result.registerDevice&&!result.allowProvisioningUpdates)throw Error('Registering an iPhone requires Allow Xcode provisioning updates');
 return result;
}
function generatedRoot(project){return path.resolve(project.root,'.langmake','studio','ios');}
function contained(project,file){
 if(typeof file!=='string'||!path.isAbsolute(file))throw Error('iOS artifact must have an absolute path');
 const root=generatedRoot(project),relative=path.relative(root,path.resolve(file));
 if(!relative||relative.startsWith('..'+path.sep)||relative==='..'||path.isAbsolute(relative))throw Error('iOS artifact must stay inside this project’s .langmake/studio/ios directory');
 return path.resolve(file);
}
async function prepareRoot(project){
 const root=await fs.realpath(project.root);if(path.resolve(root)!==path.resolve(project.root))throw Error('Project root changed; reopen the project');
 let current=root;
 for(const segment of ['.langmake','studio','ios']){current=path.join(current,segment);try{const info=await fs.lstat(current);if(info.isSymbolicLink()||!info.isDirectory())throw Error('iOS output cannot use links, junctions or non-directory paths');}catch(error){if(error.code!=='ENOENT')throw error;await fs.mkdir(current);}}
 return current;
}
async function inspectArtifact(project,file){
 const result=contained(project,file),relative=path.relative(project.root,result);let current=project.root;
 for(const segment of relative.split(path.sep)){current=path.join(current,segment);const info=await fs.lstat(current);if(info.isSymbolicLink())throw Error('iOS artifact cannot use links or junctions');}
 return result;
}
function sameStage(project,stage){const target=settings(project);if(!stage||stage.bundle_id!==target.bundleId||stage.configuration!==target.configuration||stage.orientation!==target.orientation||stage.label!==target.label)throw Error('Stage this project with its current iOS settings first');return contained(project,stage.staged);}
function sameBuild(project,report){const target=settings(project);if(!report||report.bundle_id!==target.bundleId||report.destination!==target.destination||report.arch!==target.arch||report.configuration!==target.configuration||report.orientation!==target.orientation||report.label!==target.label)throw Error('Build this project for the selected iOS target first');return contained(project,report.app);}
function device(project,id,target){
 if(typeof id!=='string'||!id||id.length>160||!/^[A-Za-z0-9-]+$/.test(id))throw Error('Choose an iPhone or simulator identifier from Devices');
 const matches=project.iosDevices?.filter(d=>d.id===id&&d.destination===target.destination)||[];
 if(matches.length!==1)throw Error('Refresh Devices and select a target in the current destination');
 const selected=matches[0];
 if(selected.available===false||selected.exclusion_reason)throw Error('The selected iOS target is unavailable; reconnect it and refresh Devices');
 if(target.destination==='device'&&((selected.reality!==undefined&&selected.reality!=='physical')||selected.reachable===false||(selected.pairing_state!=null&&selected.pairing_state!=='paired')))throw Error('Select an available paired physical iPhone from Devices');
 return id;
}
function command(script,project,action,selected='',{engine,shaderc,host=process.platform,player}={}){
 if(!ACTIONS.includes(action))throw Error('Unsupported iOS action');
 if(host!=='darwin'&&!['doctor','stage'].includes(action))throw Error('iOS build and device operations require a Mac with Xcode. Stage the project here, then use scripts/ios/ios.py on the Mac.');
 const target=settings(project),args=[script,action],root=generatedRoot(project);
 if(action==='devices')args.push('--destination',target.destination);
 if(action==='stage'){
  if(!engine)throw Error('Choose a Langmake runtime before staging an iOS project');
  args.push('--project',project.root,'--bundle-id',target.bundleId,'--label',target.label,'--orientation',target.orientation,'--configuration',target.configuration,'--output',path.join(root,'stage-'+randomUUID()),'--host-build',path.dirname(engine));
 }
 if(action==='build'){
  args.push('--staged',sameStage(project,project.iosStage),'--destination',target.destination,'--arch',target.arch,'--output',path.join(root,'build-'+randomUUID()));
  if(target.destination==='device'){
   if(!target.teamId)throw Error('Set an Apple signing Team ID before building for an iPhone');
   args.push('--team-id',target.teamId);
   if(selected)args.push('--device',device(project,selected,target));
   if(target.allowProvisioningUpdates)args.push('--allow-provisioning-updates');
   if(target.registerDevice){if(!selected)throw Error('Choose a physical iPhone from Devices before registering it with Apple');args.push('--register-device');}
  }
  if(shaderc)args.push('--shaderc',shaderc);
 }
 if(['install','launch'].includes(action)){
  const app=sameBuild(project,project.iosBuild);args.push('--destination',target.destination,'--device',device(project,selected,target));
  if(action==='install')args.push('--app',app);else args.push('--bundle-id',target.bundleId);
 }
 if(action==='stop'){
  if(!player?.ios)throw Error('No iOS play session was launched by this Studio session');
  args.push('--destination',player.destination,'--device',player.device,'--bundle-id',player.bundleId);
  if(player.destination==='device'){if(!Number.isInteger(player.pid)||player.pid<1)throw Error('The launch result did not include a process ID; stop the game on the iPhone');args.push('--pid',String(player.pid));}
 }
 return args;
}
module.exports={ACTIONS,settings,command,generatedRoot,contained,prepareRoot,inspectArtifact};
