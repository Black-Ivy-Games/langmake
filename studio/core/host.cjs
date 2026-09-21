'use strict';
const path=require('node:path');
function executableFilters(label,host=process.platform){return host==='win32'?[{name:label,extensions:['exe']}]:undefined;}
function runtimeCandidates(studioRoot,executable,{host=process.platform,override=process.env.LANGMAKE_ENGINE}={}){
 const name=host==='win32'?'langmake.exe':'langmake',folder=path.dirname(executable);
 return [override,
  ...(host==='darwin'?[path.join(studioRoot,'../.cache/ios/host/langmake'),path.join(studioRoot,'../.cache/ios/host/Release/langmake')]:[]),
  path.join(studioRoot,'../build',name),path.join(studioRoot,'../.cache/build-0.4.1',name),path.join(studioRoot,'../.cache/build-0.4',name),
  path.join(folder,'runtime',name),path.join(folder,name),path.join(folder,'../Langmake-0.5.0',name),path.join(folder,'../Langmake-0.4.1',name)].filter(Boolean);
}
module.exports={runtimeCandidates,executableFilters};
