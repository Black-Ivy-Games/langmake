'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),path=require('node:path'),fs=require('node:fs/promises'),{Broker}=require('../core/broker.cjs'),{documentation}=require('../core/tools.cjs');
test('layout IPC service refuses play sessions and worker ownership before execution',async()=>{
 const root=path.resolve('studio/.cache/layout-unit-'+Date.now());const b=await new Broker({dataRoot:root}).init();b.playing.set('game',{});await assert.rejects(b.uiEditor('game',{action:'apply'}),/human is playing/);b.playing.clear();b.running.set('worker',{projectId:'game'});await assert.rejects(b.uiEditor('game',{action:'open'}),/writer/);
});
test('layout guide is discoverable and provider context directs agents to human reviews',async()=>{
 const index=await documentation(path.resolve('build/langmake.exe'));assert(index.documents.includes('34-ui-editor-guide.md'));
 const b=new Broker({dataRoot:path.resolve("studio/.cache/layout-context-test")});const context=b.context({name:'Game',memory:{}},{messages:[]});assert(context.includes('ui-design.json'));assert(context.includes('pinned human design intent'));
});
