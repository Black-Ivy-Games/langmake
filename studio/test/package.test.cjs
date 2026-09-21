'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const {verifyRuntimePackage, copyRuntimePackage} = require('../scripts/runtime-package.cjs');

const fixtures = path.resolve(__dirname, '../.cache/package-tests');
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');

async function write(root, name, bytes) {
  const target = path.join(root, name);
  await fs.mkdir(path.dirname(target), {recursive: true});
  await fs.writeFile(target, bytes);
}

async function fixture() {
  const base = path.join(fixtures, crypto.randomUUID());
  const runtime = path.join(base, 'runtime');
  const contents = {
    'langmake.exe': 'fixture executable',
    'fonts/LatoLatin-Regular.ttf': 'fixture font',
    'shaders/vs_cubes.bin': 'fixture vertex shader',
    'shaders/fs_cubes.bin': 'fixture fragment shader',
    'scripts/workflow/workflow.py': 'fixture authoring tool',
    'examples/workflow-2d/project.json': '{"title":"Bundled example"}',
    'licenses/LatoLatin/LICENSE.txt': 'fixture license',
  };
  const manifest = {format_version: 1, product: 'Langmake', version: '0.5.0', platform: 'windows-x64', files: []};
  for (const [name, text] of Object.entries(contents)) {
    const bytes = Buffer.from(text);
    await write(runtime, name, bytes);
    manifest.files.push({path: name, bytes: bytes.length, sha256: hash(bytes)});
  }
  const saveManifest = () => write(runtime, 'package-manifest.json', JSON.stringify(manifest, null, 2));
  await saveManifest();
  return {base, runtime, destination: path.join(base, 'copied'), manifest, contents, saveManifest};
}

async function fileNames(root, prefix = '') {
  const names = [];
  for (const entry of await fs.readdir(path.join(root, prefix), {withFileTypes: true})) {
    const relative = prefix + entry.name;
    if (entry.isDirectory()) names.push(...await fileNames(root, relative + '/'));
    else names.push(relative);
  }
  return names.sort();
}

test('Studio copies every verified runtime resource and excludes extra games and secrets', async () => {
  const f = await fixture();
  const extras = ['games/private/project.json', 'games/private/brief.md', 'games/private/HANDOFF.json',
    'games/private/.langmake/targets.local.json', '.langmake/targets.local.json', '.data/credentials.json',
    '.cache/token.txt', 'credentials.txt', 'examples/workflow-2d/.langmake/saves/garden.json'];
  for (const name of extras) await write(f.runtime, name, 'private fixture; must not ship');
  const verified = await verifyRuntimePackage(f.runtime);
  await copyRuntimePackage(verified, f.destination);
  assert.deepEqual(await fileNames(f.destination), [...Object.keys(f.contents), 'package-manifest.json'].sort());
  for (const [name, text] of Object.entries(f.contents)) {
    assert.equal(await fs.readFile(path.join(f.destination, name), 'utf8'), text);
  }
  assert.deepEqual(await fs.readFile(path.join(f.destination, 'package-manifest.json')),
    await fs.readFile(path.join(f.runtime, 'package-manifest.json')));
});

test('runtime verification rejects file changes and missing inventory files', async t => {
  for (const mutation of ['same-size edit', 'size edit', 'missing']) {
    await t.test(mutation, async () => {
      const f = await fixture();
      const name = path.join(f.runtime, 'langmake.exe');
      if (mutation === 'missing') await fs.unlink(name);
      else await fs.writeFile(name, mutation === 'same-size edit' ? 'Fixture executable' : 'changed');
      await assert.rejects(verifyRuntimePackage(f.runtime), /integrity mismatch|Missing runtime package file/);
    });
  }
});

test('runtime verification rejects malformed or wrong-version inventories', async t => {
  const cases = [
    ['missing hash', f => delete f.manifest.files[0].sha256, /size or SHA-256/],
    ['invalid hash', f => { f.manifest.files[0].sha256 = 'not-a-sha256'; }, /size or SHA-256/],
    ['negative size', f => { f.manifest.files[0].bytes = -1; }, /size or SHA-256/],
    ['wrong version', f => { f.manifest.version = '0.4.1'; }, /0\.5\.0.*manifest/],
    ['wrong product', f => { f.manifest.product = 'Other'; }, /runtime package manifest/],
    ['missing executable', f => { f.manifest.files.shift(); }, /omits langmake\.exe/],
    ['duplicate case alias', f => { f.manifest.files.push({...f.manifest.files[0], path: 'LANGMAKE.EXE'}); }, /Duplicate/],
  ];
  for (const [name, mutate, message] of cases) {
    await t.test(name, async () => {
      const f = await fixture();
      mutate(f);
      await f.saveManifest();
      await assert.rejects(verifyRuntimePackage(f.runtime), message);
    });
  }
});

test('runtime inventory cannot name escaped paths, alternate streams or local state', async t => {
  for (const name of ['../outside.txt', '/absolute.txt', 'C:/outside.txt', 'fonts\\outside.txt',
    'file.txt:token', './langmake.exe', 'fonts/../langmake.exe', 'langmake.exe.',
    '.langmake/targets.local.json', 'examples/workflow-2d/.LANGMAKE/saves/garden.json',
    '.data/credentials.json', '.cache/token.txt', 'package-manifest.json']) {
    await t.test(name, async () => {
      const f = await fixture();
      f.manifest.files.push({path: name, bytes: 0, sha256: hash('')});
      await f.saveManifest();
      await assert.rejects(verifyRuntimePackage(f.runtime), /manifest path|reserved runtime manifest/);
    });
  }
});

test('runtime inventory refuses directory junctions even when the target bytes match', async () => {
  const f = await fixture();
  const external = path.join(f.base, 'external');
  await write(external, 'secret.txt', 'secret');
  await fs.symlink(external, path.join(f.runtime, 'linked'), 'junction');
  f.manifest.files.push({path: 'linked/secret.txt', bytes: 6, sha256: hash('secret')});
  await f.saveManifest();
  await assert.rejects(verifyRuntimePackage(f.runtime), /refuses links/);
});

test('runtime copy rejects manifest or file changes after preflight verification', async t => {
  await t.test('manifest changes', async () => {
    const f = await fixture();
    const verified = await verifyRuntimePackage(f.runtime);
    f.manifest.created_utc = 'changed';
    await f.saveManifest();
    await assert.rejects(copyRuntimePackage(verified, f.destination), /manifest changed/);
    await assert.rejects(fs.access(f.destination));
  });
  await t.test('source changes', async () => {
    const f = await fixture();
    const verified = await verifyRuntimePackage(f.runtime);
    await fs.writeFile(path.join(f.runtime, 'langmake.exe'), 'Fixture executable');
    await assert.rejects(copyRuntimePackage(verified, f.destination), /integrity mismatch/);
    await assert.rejects(fs.access(path.join(f.destination, 'langmake.exe')));
    await assert.rejects(fs.access(path.join(f.destination, 'package-manifest.json')));
  });
});
