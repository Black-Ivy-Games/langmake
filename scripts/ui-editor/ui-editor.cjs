#!/usr/bin/env node
'use strict';
// One JSON request makes the CLI contract identical to Studio's service.
const fs=require('node:fs/promises'),{operate}=require('./editor.cjs');
(async()=>{if(process.argv.length!==3)throw Error('Usage: node scripts/ui-editor/ui-editor.cjs REQUEST.json (root, engine, action and action fields). Requests and outputs stay in your authorized workspace.');const request=JSON.parse(await fs.readFile(process.argv[2],'utf8'));const result=await operate(request);if(result.capture)delete result.capture.image;console.log(JSON.stringify(result,null,2));})().catch(e=>{console.log(JSON.stringify({ok:false,error:e.message}));process.exitCode=1;});
