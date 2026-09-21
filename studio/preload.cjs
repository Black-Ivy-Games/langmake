'use strict';
const {contextBridge,ipcRenderer}=require('electron');
contextBridge.exposeInMainWorld('studio',Object.freeze({request:(method,args={})=>ipcRenderer.invoke('studio:request',method,args),onEvent:callback=>{const listener=(_event,value)=>callback(value);ipcRenderer.on('studio:event',listener);return()=>ipcRenderer.removeListener('studio:event',listener);}}));
