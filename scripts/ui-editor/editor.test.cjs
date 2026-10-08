'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs/promises'),path=require('node:path'),{patch,scoped,config}=require('./editor.cjs');
test('patch preserves actions, text, nested tags, comments and unselected source',()=>{
 const source='<!-- <button id="go"> -->\n<button id="go" data-action="fire" title="a > b"><span>Fire &amp; hold</span></button><p id="other">Score</p>';
 const edited=patch(source,'go',{right:'24dp',position:'absolute',color:'#ffaa99'});
 assert(edited.includes('data-action="fire" title="a > b"'));assert(edited.includes('<span>Fire &amp; hold</span>'));assert(edited.endsWith('<p id="other">Score</p>'));assert(edited.startsWith('<!-- <button id="go"> -->'));
 assert.equal(patch(edited,'go',{right:null,position:null,color:null}),source);
});
test('duplicates, unsupported values and complex inline styles diagnose',()=>{
 for(const [s,id,p] of [['<b id="x"/><b id="x"/>','x',{left:'1dp'}],['<b id="x"/>','x',{left:'url(file:///private)'}],['<b id="x"/>','x',{width:'-1dp'}],['<b id="x" style="transform: rotate(10deg)"/>','x',{left:'2dp'}],['<b id="x"/>','x',{onclick:'hello'}]])assert.throws(()=>patch(s,id,p));
 assert.throws(()=>config({width:9999}));assert.throws(()=>config({tick:Infinity}));
});
test('path containment rejects traversal, protected data, absolute paths and links',async()=>{
 const base=path.resolve('.cache/ui-editor/path-test');await fs.mkdir(base,{recursive:true});
 for(const p of ['../escape','C:/secret','.git/config','foo:stream'])await assert.rejects(scoped(base,p,true));
 const target=await fs.mkdtemp(path.join(base,'case-'));await fs.symlink(target,path.join(target,'link'),'junction');await assert.rejects(scoped(base,path.relative(base,path.join(target,'link/a')),true),/Linked/);
});
