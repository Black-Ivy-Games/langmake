'use strict';

const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');

const manifestName = 'package-manifest.json';
const privateDirectories = new Set(['.langmake', '.cache', '.data', '.git', '__pycache__']);

function packagePath(name) {
  if (typeof name !== 'string' || !name || /[\\:\x00-\x1f]/.test(name)) {
    throw Error('Invalid runtime manifest path: ' + name);
  }
  const parts = name.split('/');
  if (parts.some(part => !part || part === '.' || part === '..' || /[. ]$/.test(part))) {
    throw Error('Invalid runtime manifest path: ' + name);
  }
  if (parts.some(part => privateDirectories.has(part.toLowerCase()))) {
    throw Error('Private runtime manifest path: ' + name);
  }
  return parts;
}

async function readPackageFile(root, name) {
  const parts = packagePath(name);
  let file = root;
  for (const [index, part] of parts.entries()) {
    file = path.join(file, part);
    let info;
    try {
      info = await fs.lstat(file);
    } catch (error) {
      throw Error('Missing runtime package file: ' + name, {cause: error});
    }
    if (info.isSymbolicLink() || (index < parts.length - 1 ? !info.isDirectory() : !info.isFile())) {
      throw Error('Runtime package refuses links or non-files: ' + name);
    }
  }
  return fs.readFile(file);
}

async function verifiedBytes(root, entry) {
  const bytes = await readPackageFile(root, entry.path);
  if (bytes.length !== entry.bytes || crypto.createHash('sha256').update(bytes).digest('hex') !== entry.sha256) {
    throw Error('Runtime package integrity mismatch: ' + entry.path);
  }
  return bytes;
}

async function verifyRuntimePackage(runtime, expectedVersion = '0.5.0') {
  const root = await fs.realpath(runtime);
  const manifestBytes = await readPackageFile(root, manifestName);
  const manifest = JSON.parse(manifestBytes.toString('utf8'));
  if (manifest.format_version !== 1 || manifest.product !== 'Langmake' ||
      manifest.version !== expectedVersion || manifest.platform !== 'windows-x64' ||
      !Array.isArray(manifest.files) || !manifest.files.length) {
    throw Error('Expected a Langmake ' + expectedVersion + ' windows-x64 runtime package manifest');
  }
  const seen = new Set();
  for (const entry of manifest.files) {
    packagePath(entry?.path);
    const name = entry.path.toLowerCase();
    if (seen.has(name) || name === manifestName) {
      throw Error('Duplicate or reserved runtime manifest entry: ' + entry.path);
    }
    seen.add(name);
    if (!Number.isSafeInteger(entry.bytes) || entry.bytes < 0 ||
        typeof entry.sha256 !== 'string' || !/^[a-f0-9]{64}$/.test(entry.sha256)) {
      throw Error('Invalid runtime manifest size or SHA-256: ' + entry.path);
    }
    await verifiedBytes(root, entry);
  }
  if (!seen.has('langmake.exe')) throw Error('Runtime manifest omits langmake.exe');
  return {root, manifest, manifestBytes};
}

async function copyRuntimePackage(verified, destination) {
  // Recheck after Electron packaging: an edited input must not bypass the inventory.
  const manifestBytes = await readPackageFile(verified.root, manifestName);
  if (!manifestBytes.equals(verified.manifestBytes)) throw Error('Runtime package manifest changed during packaging');
  await fs.mkdir(destination);
  for (const entry of verified.manifest.files) {
    const bytes = await verifiedBytes(verified.root, entry);
    const target = path.join(destination, ...packagePath(entry.path));
    await fs.mkdir(path.dirname(target), {recursive: true});
    await fs.writeFile(target, bytes, {flag: 'wx'});
  }
  await fs.writeFile(path.join(destination, manifestName), verified.manifestBytes, {flag: 'wx'});
}

module.exports = {verifyRuntimePackage, copyRuntimePackage};
