'use strict';
(()=>{
 let draft=null,owner='',selected='',busy=false,info=null,bitmap=null,drag=null;
 const get=id=>document.getElementById(id),canvas=get('layoutCanvas'),ctx=canvas.getContext('2d');
 const status=t=>get('layoutStatus').textContent=t;
 async function request(action,extra={}){
  if(busy)return;busy=true;buttons();status('Working: '+action+'…');
  try{const r=await window.studio.request('uiEditor',{projectId:owner,request:{action,...(draft?{session:draft.session,version:draft.version}:{}),...extra}});
   if(r.session){draft=r;if(action==='open'||action==='read')restoreConfig();if(!r.elements.some(e=>e.id===selected))selected=r.elements[0]?.id||'';await render();}
   if(action==='export')status('Agent review saved: '+r.review+' · read ui-design.json');else status(action==='apply'?'Applied to '+r.document+'. Open a new draft for further edits.':'Draft is isolated. Apply saves the RML; Export gives the agent your screenshot and notes.');
   return r;
  }catch(e){
   // A native capture can fail after an edit was saved. Recover its latest version
   // without replaying the mutation so Refresh and Undo remain usable.
   if(draft&&!['discover','open','read','discard'].includes(action))try{const r=await window.studio.request('uiEditor',{projectId:owner,request:{action:'read',session:draft.session}});draft=r;await render();}catch{}
   status(e.message.replace(/^Error invoking remote method '[^']+': Error: /,''));
  }
  finally{busy=false;buttons();}
 }
 function buttons(){for(const b of get('layoutDialog').querySelectorAll('button'))if(b.id!=='layoutClose')b.disabled=busy;
  get('layoutUndo').disabled=busy||!draft?.canUndo;get('layoutRedo').disabled=busy||!draft?.canRedo;
  get('layoutApply').disabled=busy||!draft||draft.applied||!draft.dirty;get('layoutExport').disabled=busy||!draft?.capture||draft.capture.stale;
  get('layoutPropertiesApply').disabled=busy||!draft||draft.applied;get('layoutRefresh').disabled=busy||!draft;get('layoutDiscard').disabled=busy||!draft;
  get('layoutNoteSave').disabled=busy||!draft;get('layoutAnchor').disabled=busy||!draft||draft.applied;
 }
 function element(){return draft?.capture?.state.ui_renderer.elements[selected];}
 function fields(){get('layoutFields').replaceChildren();const authored=draft?.elements.find(e=>e.id===selected),observed=element();get('layoutSelected').textContent=selected||'Select an element';
  get('layoutElementInfo').textContent=authored?.diagnostic||(!observed?'Not present at this tick; use a timeline to reach its state.':`${observed.tag} · ${observed.visible?'visible':'hidden'} · ${Math.round(observed.size[0])} × ${Math.round(observed.size[1])} px`);
  for(const key of draft?.properties||[]){const label=document.createElement('label');label.textContent=key;const input=document.createElement('input');input.dataset.property=key;input.setAttribute('aria-label',key);input.value=authored?.inline[key]||'';input.placeholder=observed?.properties?.[key]||'stylesheet';input.disabled=draft.applied||authored?.editable===false;label.append(input);get('layoutFields').append(label);}
  get('layoutNote').value=draft?.notes[selected]?.text||'';get('layoutLock').checked=!!draft?.notes[selected]?.locked;get('layoutAnchor').value='';
 }
 async function render(){
  get('layoutDocumentName').textContent=draft.document+(draft.dirty?' · modified':'')+(draft.applied?' · applied':'');get('layoutSource').textContent='ORIGINAL\n'+draft.original+'\n\nDRAFT\n'+draft.content;
  get('layoutElements').replaceChildren();for(const e of draft.elements){const b=document.createElement('button');b.textContent=e.id+(draft.notes[e.id]?.locked?' · pinned':'');b.classList.toggle('selected',e.id===selected);b.onclick=()=>{selected=e.id;render().catch(e=>status(e.message));};get('layoutElements').append(b);}
  fields();if(draft.capture){const image=new Image();image.src=draft.capture.image;await image.decode();bitmap=image;canvas.width=draft.config.width;canvas.height=draft.config.height;draw();}
 }
 function draw(rect){ctx.clearRect(0,0,canvas.width,canvas.height);if(bitmap)ctx.drawImage(bitmap,0,0,canvas.width,canvas.height);if(!draft)return;
  const ui=draft.capture?.state.ui_renderer;if(!ui)return;ctx.lineWidth=Math.max(2,draft.config.density*1.5);ctx.strokeStyle='#75d6b9';ctx.setLineDash([8,8]);ctx.strokeRect(...ui.safe_area);ctx.setLineDash([]);
  const e=element();if(!rect&&e)rect=[...e.position,...e.size];if(rect){ctx.strokeStyle=drag?'#ffd57a':'#75d6ff';ctx.strokeRect(...rect);ctx.fillStyle=ctx.strokeStyle;ctx.fillRect(rect[0]+rect[2]-10,rect[1]+rect[3]-10,10,10);ctx.beginPath();ctx.moveTo(rect[0],0);ctx.lineTo(rect[0],canvas.height);ctx.moveTo(0,rect[1]);ctx.lineTo(canvas.width,rect[1]);ctx.globalAlpha=.28;ctx.stroke();ctx.globalAlpha=1;}
 }
 function point(e){const r=canvas.getBoundingClientRect();return [(e.clientX-r.left)*canvas.width/r.width,(e.clientY-r.top)*canvas.height/r.height];}
 function snapped(x){const dp=x/draft.config.density,grid=get('layoutSnap').checked?8:1;return Math.round(dp/grid)*grid;}
 function dp(n){return Number(n.toFixed(3))+'dp';}
 function moveProperties(e,dx,dy){const c=e.properties,d=draft.config.density;const out={};
  for(const [near,far,delta,axis] of [['left','right',dx,0],['top','bottom',dy,1]]){const key=c[near]!=='auto'?near:far;const value=c[key]||'auto';const m=value.match(/^(-?[\d.]+)(dp|px|%)$/);if(!m)throw Error('Choose an anchor first; this offset is not a simple dp/px/% value');const sign=key===near?1:-1;const size=e.offset_parent_rect[axis+2];let change=snapped(delta);if(m[2]==='px')change*=d;else if(m[2]==='%')change=change*d/size*100;out[key]=Number((Number(m[1])+sign*change).toFixed(3))+m[2];}
  return out;
 }
 canvas.onpointerdown=e=>{if(busy||!draft||draft.applied)return;const [x,y]=point(e),all=draft.capture?.state.ui_renderer.elements||{};
  const ids=draft.elements.map(t=>t.id).filter(id=>{const n=all[id];return n?.visible&&x>=n.position[0]&&y>=n.position[1]&&x<=n.position[0]+n.size[0]&&y<=n.position[1]+n.size[1];}).sort((a,b)=>all[a].size[0]*all[a].size[1]-all[b].size[0]*all[b].size[1]);
  const current=element(),handle=current&&Math.abs(x-current.position[0]-current.size[0])<16&&Math.abs(y-current.position[1]-current.size[1])<16;
  if(!handle&&ids.length)selected=ids[0];fields();draw();const n=element();if(!n)return;if(n.transformed){status('Transformed elements require source positioning; drag is disabled.');return;}if(n.properties.position!=='absolute'){status('Flow element: edit spacing or choose an anchor before dragging.');return;}
  drag={x,y,rect:[...n.position,...n.size],element:n,resize:!!handle};canvas.setPointerCapture(e.pointerId);e.preventDefault();
 };
 canvas.onpointermove=e=>{if(!drag)return;const [x,y]=point(e),dx=snapped(x-drag.x)*draft.config.density,dy=snapped(y-drag.y)*draft.config.density,r=[...drag.rect];if(drag.resize){r[2]=Math.max(8,r[2]+dx);r[3]=Math.max(8,r[3]+dy);}else{r[0]+=dx;r[1]+=dy;}draw(r);};
 canvas.onpointerup=async e=>{if(!drag)return;const current=drag;drag=null;const [x,y]=point(e),dx=x-current.x,dy=y-current.y;if(Math.abs(dx)+Math.abs(dy)<3){draw();return;}
  try{const properties=current.resize?{width:dp(Math.max(1,current.element.content_size[0]/draft.config.density+snapped(dx))),height:dp(Math.max(1,current.element.content_size[1]/draft.config.density+snapped(dy)))}:moveProperties(current.element,dx,dy);await request('patch',{id:selected,properties});}catch(err){status(err.message);draw();}
 };
 canvas.onpointercancel=()=>{drag=null;draw();};
 get('layoutAnchor').onchange=async()=>{const value=get('layoutAnchor').value,e=element();if(!value||!e)return;if(e.transformed){status('Transformed elements require source positioning.');return;}const d=draft.config.density,[x,y,w,h]=e.offset_parent_rect;const right=value.includes('right'),bottom=value.includes('bottom');
  await request('patch',{id:selected,properties:{position:'absolute',left:right?'auto':dp((e.position[0]-x)/d),right:right?dp((x+w-e.position[0]-e.size[0])/d):'auto',top:bottom?'auto':dp((e.position[1]-y)/d),bottom:bottom?dp((y+h-e.position[1]-e.size[1])/d):'auto','margin-left':'0dp','margin-right':'0dp','margin-top':'0dp','margin-bottom':'0dp'}});
 };
 get('layoutPropertiesApply').onclick=()=>{const original=draft.elements.find(e=>e.id===selected)?.inline||{},properties={};for(const input of get('layoutFields').querySelectorAll('input'))if(input.value!==(original[input.dataset.property]||''))properties[input.dataset.property]=input.value||null;request('patch',{id:selected,properties});};
 get('layoutNoteSave').onclick=()=>request('note',{id:selected,text:get('layoutNote').value,locked:get('layoutLock').checked});
 get('layoutUndo').onclick=()=>request('undo');get('layoutRedo').onclick=()=>request('redo');get('layoutApply').onclick=()=>request('apply');get('layoutExport').onclick=()=>request('export');get('layoutRefresh').onclick=()=>request('capture');
 get('layoutDiscard').onclick=async()=>{const r=await request('discard');if(r){draft=null;bitmap=null;get('layoutElements').replaceChildren();get('layoutFields').replaceChildren();draw();buttons();status('Draft discarded. Source edits previously applied remain saved.');}};
 function newConfig(){return {profile:get('layoutProfile').value,width:Number(get('layoutWidth').value),height:Number(get('layoutHeight').value),density:Number(get('layoutDensity').value),tick:Number(get('layoutTick').value),input:get('layoutInput').value.trim(),safe_area:get('layoutSafe').value.trim()};}
 function restoreConfig(){const c=draft.config;for(const [id,key] of [['layoutWidth','width'],['layoutHeight','height'],['layoutDensity','density'],['layoutTick','tick'],['layoutInput','input'],['layoutSafe','safe_area'],['layoutProfile','profile']])get(id).value=c[key];get('layoutDocument').value=draft.document;get('layoutPreset').value=c.width===1280&&c.height===720&&c.density===1?'desktop':c.width===780&&c.height===1440&&c.density===2?'phone':c.width===1536&&c.height===1152&&c.density===2?'tablet':'custom';}
 get('layoutOpenDraft').onclick=async()=>{if(draft&&!draft.applied&&draft.dirty){status('Apply or discard the current modified draft before changing its preview.');return;}if(draft){const r=await request('discard');if(!r)return;draft=null;}await request('open',{document:get('layoutDocument').value,config:newConfig()});};
 get('layoutPreset').onchange=()=>{const presets={desktop:[1280,720,1],phone:[780,1440,2],tablet:[1536,1152,2]};const p=presets[get('layoutPreset').value];if(p){get('layoutWidth').value=p[0];get('layoutHeight').value=p[1];get('layoutDensity').value=p[2];}};
 get('layoutResume').onclick=async()=>{const id=get('layoutDrafts').value;if(!id)return;await request('read',{session:id});};
 get('layoutClose').onclick=()=>{if(busy){status('Wait for the native operation to finish.');return;}get('layoutDialog').close();};get('layoutDialog').oncancel=e=>{if(busy)e.preventDefault();};
 get('openLayout').onclick=async()=>{if(!projectId)return;const changed=owner!==projectId;owner=projectId;if(changed){draft=null;bitmap=null;selected='';}get('layoutDialog').showModal();info=await request('discover');if(!info)return;get('layoutDrafts').replaceChildren(new Option('Saved drafts',''));for(const d of info.drafts)get('layoutDrafts').append(new Option(d.document+' · '+(d.applied?'applied':'draft')+' · '+d.session.slice(0,8),d.session));get('layoutProfile').replaceChildren(new Option('Project default',''));for(const id of info.profiles)get('layoutProfile').append(new Option(id,id));get('layoutDocument').replaceChildren(new Option('First registered document',''));for(const name of info.documents)get('layoutDocument').append(new Option(name,name));if(draft)await render();buttons();};
})();
