// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Builds resources.json for the ad blocker: the scriptlets and the stand-in
// resources (empty scripts, 1x1 images, ...) that filters such as
//   example.com##+js(set-constant, adsEnabled, false)
//   ||ads.example.com/pixel.gif$redirect=1x1.gif
// refer to. They come from uBlock Origin, which keeps them as JavaScript
// modules, so this runs them rather than parsing them.
//
// uBlock Origin is GPL-3.0. The result is published on the update server and
// downloaded by the browser when it is running, not shipped inside it.
//
//   node build_resources.mjs --out resources.json [--ref master]
//                            [--ubo <path to a uBlock checkout>]
//
// Needs Node 20+ (or Bun). Downloads from raw.githubusercontent.com unless
// --ubo is given.

import {createHash} from 'node:crypto';
import {mkdirSync, readFileSync, writeFileSync, existsSync, readdirSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join, dirname, posix} from 'node:path';
import {pathToFileURL} from 'node:url';
import {Script} from 'node:vm';

const args = new Map();
for (let i = 2; i < process.argv.length; i += 2) {
  args.set(process.argv[i].replace(/^--/, ''), process.argv[i + 1]);
}
const outPath = args.get('out') || 'resources.json';
const ref = args.get('ref') || 'master';

const RAW = `https://raw.githubusercontent.com/gorhill/uBlock/${ref}`;
const API = 'https://api.github.com/repos/gorhill/uBlock/contents';

async function fetchBytes(url) {
  const response = await fetch(url, {headers: {'user-agent': 'reba-adblock-resources'}});
  if (!response.ok) {
    throw new Error(`${response.status} ${url}`);
  }
  return new Uint8Array(await response.arrayBuffer());
}

async function listDir(path) {
  const response = await fetch(`${API}/${path}?ref=${ref}`, {
    headers: {'user-agent': 'reba-adblock-resources'},
  });
  if (!response.ok) {
    throw new Error(`${response.status} listing ${path}`);
  }
  return (await response.json()).filter(e => e.type === 'file').map(e => e.name);
}

// Gets the part of uBlock Origin's tree that is needed, into a temp folder.
async function checkout() {
  const root = join(tmpdir(), `reba-ubo-${ref.replace(/\W/g, '_')}`);
  mkdirSync(root, {recursive: true});
  // Lets Node load the files as ES modules.
  writeFileSync(join(root, 'package.json'), '{"type":"module"}');
  const want = [];
  for (const name of await listDir('src/js/resources')) {
    want.push(`src/js/resources/${name}`);
  }
  for (const name of await listDir('src/web_accessible_resources')) {
    want.push(`src/web_accessible_resources/${name}`);
  }
  want.push('src/js/redirect-resources.js');
  const fetched = new Set();
  const fetchAll = async paths => {
    await Promise.all(paths.map(async path => {
      if (fetched.has(path)) {
        return;
      }
      fetched.add(path);
      const file = join(root, path);
      mkdirSync(dirname(file), {recursive: true});
      writeFileSync(file, await fetchBytes(`${RAW}/${path}`));
    }));
  };
  await fetchAll(want);
  // Some modules import others from further up the tree. Fetch those too,
  // until nothing is missing.
  const importRe = /(?:from|import)\s*['"](\.{1,2}\/[^'"]+\.js)['"]/g;
  for (;;) {
    const missing = new Set();
    for (const path of fetched) {
      if (!path.endsWith('.js')) {
        continue;
      }
      const source = readFileSync(join(root, path), 'utf8');
      for (const match of source.matchAll(importRe)) {
        const target = posix.normalize(posix.join(posix.dirname(path), match[1]));
        if (!fetched.has(target)) {
          missing.add(target);
        }
      }
    }
    if (missing.size === 0) {
      break;
    }
    await fetchAll([...missing]);
  }
  return root;
}

const mimes = new Map([
  ['css', 'text/css'], ['gif', 'image/gif'], ['html', 'text/html'],
  ['js', 'application/javascript'], ['json', 'application/json'],
  ['mp3', 'audio/mp3'], ['mp4', 'video/mp4'], ['png', 'image/png'],
  ['txt', 'text/plain'], ['xml', 'text/xml'],
]);

function mimeFor(name) {
  const dot = name.lastIndexOf('.');
  const extension = dot < 0 ? '' : name.slice(dot + 1);
  if (extension === 'fn') {
    return 'fn/javascript';
  }
  return mimes.get(extension) || 'application/octet-stream';
}

function b64(data) {
  return Buffer.from(data).toString('base64');
}

const root = args.get('ubo') || await checkout();
const moduleUrl = path => pathToFileURL(join(root, path)).href;

// A few pieces of the browser the modules mention at load time.
globalThis.self ??= globalThis;

const resources = [];
const names = new Set();
function add(resource) {
  if (names.has(resource.name)) {
    console.warn(`skipping ${resource.name}: the name is taken`);
    return;
  }
  names.add(resource.name);
  // A taken alias is dropped; the resource is still worth having.
  resource.aliases = resource.aliases.filter(alias => {
    if (names.has(alias)) {
      console.warn(`${resource.name}: alias ${alias} is taken`);
      return false;
    }
    names.add(alias);
    return true;
  });
  resources.push(resource);
}

// --- Scriptlets -----------------------------------------------------------

// The name scriptlets know their state by, defined by uBlock Origin's wrapper
// around the code it injects.
const GLOBALS_RESOURCE = 'scriptlet-globals.fn';
add({
  name: GLOBALS_RESOURCE,
  aliases: [],
  kind: {mime: 'fn/javascript'},
  content: b64('const $SCRIPTLET_GLOBALS$ = {logLevel: 1};\n' +
               'const scriptletGlobals = $SCRIPTLET_GLOBALS$;\n'),
  dependencies: [],
});

// Filters ported from other blockers refer to the scriptlets as `ubo-<name>`.
function withUboAliases(name, aliases) {
  if (name.endsWith('.fn')) {
    return aliases;
  }
  return [...aliases, ...[name, ...aliases].map(n => `ubo-${n}`)];
}

const {builtinScriptlets} = await import(moduleUrl('src/js/resources/scriptlets.js'));
let scriptletCount = 0;
for (const scriptlet of builtinScriptlets) {
  if (scriptlet.world && /^isolated$/i.test(scriptlet.world)) {
    continue;  // Can't run in the page's own world.
  }
  const source = scriptlet.fn.toString();
  const dependencies = (scriptlet.dependencies || []).filter(d => typeof d === 'string');
  if (!dependencies.includes(GLOBALS_RESOURCE)) {
    dependencies.push(GLOBALS_RESOURCE);
  }
  const resource = {
    name: scriptlet.name,
    aliases: withUboAliases(scriptlet.name, scriptlet.aliases || []),
    kind: {mime: mimeFor(scriptlet.name)},
    content: b64(source),
    dependencies,
  };
  if (scriptlet.requiresTrust) {
    // Bit 0 of the permission mask: trusted lists only.
    resource.permission = 1;
  }
  add(resource);
  scriptletCount++;
}

// --- Redirect resources ---------------------------------------------------

const redirects = (await import(moduleUrl('src/js/redirect-resources.js'))).default;
let redirectCount = 0;
for (const [name, details] of redirects) {
  if (details.params) {
    continue;  // Needs parameters, which the engine doesn't support.
  }
  const file = join(root, 'src/web_accessible_resources', name);
  if (!existsSync(file)) {
    console.warn(`missing file for ${name}`);
    continue;
  }
  let data = readFileSync(file);
  const mime = mimeFor(name);
  if (mime === 'application/javascript' || mime === 'text/html' || mime === 'text/plain') {
    data = Buffer.from(data.toString('utf8').replace(/\r/g, ''));
  }
  const aliases = details.alias === undefined ? [] : [].concat(details.alias);
  add({name, aliases, kind: {mime}, content: b64(data), dependencies: []});
  redirectCount++;
}

// --- Check that what we make runs ----------------------------------------

// Put together what the engine would inject for each scriptlet with no
// arguments, and make sure it parses wrapped the way the browser runs it:
// each engine's scriptlets in a block of their own (EngineSet), all inside one
// strict mode function (CosmeticObserver). Strict mode rejects some things
// that a plain script allows, so check it the same way.
const byName = new Map(resources.map(r => [r.name, r]));
const wrapLikeTheBrowser = code =>
    `(function(){'use strict';try{\n{\n${code}\n}\n}catch(e){}})();`;
function collect(resource, into) {
  for (const dep of resource.dependencies) {
    const d = byName.get(dep);
    if (d && !into.has(d.name)) {
      into.set(d.name, d);
      collect(d, into);
    }
  }
}
let broken = 0;
for (const resource of resources) {
  if (resource.kind.mime !== 'application/javascript' || resource.dependencies.length === 0) {
    continue;
  }
  const deps = new Map();
  collect(resource, deps);
  const code = [...deps.values()].map(d => Buffer.from(d.content, 'base64').toString()).join('\n') +
      '\n' + Buffer.from(resource.content, 'base64').toString();
  try {
    new Script(wrapLikeTheBrowser(code));  // Compiles only; nothing runs.
  } catch (e) {
    console.warn(`syntax error in ${resource.name}: ${e.message}`);
    broken++;
  }
}

const json = JSON.stringify(resources);
writeFileSync(outPath, json);
const hash = createHash('sha256').update(json).digest('hex');
console.log(`${scriptletCount} scriptlets, ${redirectCount} redirect resources` +
            `${broken ? `, ${broken} with syntax errors` : ''}`);
console.log(`${outPath}: ${json.length} bytes, sha256 ${hash}`);
process.exit(broken ? 1 : 0);
