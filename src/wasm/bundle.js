#!/usr/bin/env node
/*
 * Copyright 2020 Sam Thorogood.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy of
 * the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations under
 * the License.
 */

/**
 * @fileoverview Node interface to a JS bundler.
 */

import fs from 'fs';
import {specials, types, hashes} from './wrap.js';
import build from './wrap.js';
import path from 'path';
import stream from 'stream';

import {processFile} from '../bundler/file.js';


function nameGlobal(globals, name) {
  let base;
  if (name === '*') {
    base = 'all';
  } else if (name === '') {
    base = 'default';
  } else {
    base = name;
  }
  let update = base;
  let count = 0;

  while (globals.has(update)) {
    update = `${base}$${++count}`;
  }
  globals.add(update);
  return update;
}


function resolve(other, importer) {
  if (/^\.{0,2}\//.test(other)) {
    return path.join(path.dirname(importer), other);
  }
  return other;
}


function internalBundle(runner, files) {
  const readable = new stream.Readable({emitClose: true});
  const write = readable.push.bind(readable);

  const globals = new Set(['', 'default', '*']);

  const fileData = new Map();
  const unbundledImports = new Map();

  // Recursively process every input file. Match ESM execution order.
  const processed = new Set();
  const process = (f) => {
    if (processed.has(f)) {
      return false;
    }
    processed.add(f);

    if (!fs.existsSync(f)) {
      unbundledImports.set(f, {});
      return true;;
    }

    const buffer = fs.readFileSync(f);
    const {deps, externals, imports, exports, render} = processFile(runner, buffer);

    deps.forEach((dep) => {
      const resolved = resolve(dep, f);
      process(resolved);
    });

    externals.forEach((external) => globals.add(external));

    // processFile doesn't know how to resolve external imports; resolve here.
    for (const name in imports) {
      const d = imports[name];
      d.from = resolve(d.from, f);
    }

    fileData.set(f, {imports, exports, render});
  };
  for (const f of files) {
    process(path.resolve(f));
  }

  // Shared registry for variable renames.
  const registry = {};
  const register = (name, f) => {
    const key = `${name}~${f}`;

    const prev = registry[key];
    if (prev !== undefined) {
      return prev;
    }
    const update = nameGlobal(globals, name);
    registry[key] = update;
    return update;
  };

  for (const [f, data] of fileData) {
    const {imports, exports, render} = data;
    const renames = {};

    for (const x in imports) {
      let {from, name} = imports[x];

      // If the referenced export comes from within our bundle, then prefer its original name.
      const isBundledExport = fileData.has(from);
      if (isBundledExport) {
        const {exports: otherExports} = fileData.get(from);
        const real = otherExports[name];
        if (real === undefined) {
          throw new Error(`${from} does not export: ${name}`);
        }
        name = real;
      }

      const update = register(name, from);
      renames[x] = update;

      // If the export is unbundled, we need to record what it's called so we can import it later.
      if (!isBundledExport) {
        const mapping = unbundledImports.get(from);
        mapping[name] = update;
      }
    }

    for (const x in exports) {
      const real = exports[x];
      renames[real] = register(real, f);
    }

    render(write, (name) => {
      const update = renames[name];
      if (update !== undefined) {
        return update;
      }

      // Otherwise, this is local to this file: just make it unique to our bundle.
      return nameGlobal(globals, name);
    });
  }

  // TODO(samthor): While allowed, this is the opposite of fast, and should be at the top.
  for (const [f, mapping] of unbundledImports) {
    write(`${rebuildModuleDeclaration(false, mapping, f)}\n`);
  }

  readable.push(null);
  return readable;
}

/**
 *
 */
function internalBundleOld(runner, files) {
  const {token} = runner;
  const readable = new stream.Readable({emitClose: true});

  const toplevels = new Set(['', 'default', '*']);  // specials that cannot really exist
  const externals = new Map();
  const globsFor = new Map();

  const fileQueue = new Set();
  for (const f of files) {
    fileQueue.add(path.resolve(f));
  }
  const queue = (f, other, mapping) => {
    if (/^\.\.?\//.test(other)) {
      // Make 'other' an absolute path.
      other = path.join(path.dirname(f), other);
      if (fs.existsSync(other)) {
        fileQueue.add(other);
        return {bundled: true, resolved: other};
      }
    }

    // Otherwise, this is an external.
    let known = externals.get(other);
    if (known === undefined) {
      known = {};
      externals.set(other, known);
    }

    // Invert the mapping struct, with first-wins semantics. We need to use the imported
    // name as we assume the user has chosen a valid one.
    for (const key in mapping) {
      const cand = mapping[key];
      if (cand === '*') {
        // TODO: do we need to include this?
      }
      if (!(cand in known)) {
        known[cand] = key;
      }
    }

    return {bundled: false, resolved: other};
  };

  const fileData = new Map();

  // Pass #1: process all files. Read their imports/exports.
  for (const f of fileQueue) {
    // nb. read here, because we need all buffers at end (can't reuse wasm space)
    const buffer = fs.readFileSync(f);
    const sharedBuffer = runner.prepare(buffer.length);
    sharedBuffer.set(buffer);

    const scopes = [new Scope(null)];
    const tops = [0];
    let top = scopes[0];
    let scope = scopes[0];

    const refs = {};

    const callback = (special) => {
      if (special & specials.declare) {
        const cand = token.string();
        const where = (special & specials.top) ? top : scope;
        where.mark(token, true);

        if (special & specials.top) {
          if (cand in top.vars) {
            // This is a hoisted var use, we probably don't care.
          }
        }
        // nb. Treat "let" and "const" like "var" w.r.t. hoisting. Use like this is invalid (TDZ)
        // so there's nothing we can do anyway.

      } else if (token.type() === types.symbol) {
        scope.mark(token, false);
      }

      if (token.type() !== types.keyword) {
        return;
      }
      const h = token.hash();
      if (!(h === hashes.import || h === hashes.export)) {
        return;
      }

      const start = token.at();
      const defaultExportHoist = Boolean(special & specials.defaultHoist);

      if (h === hashes.export) {
        exportHandler(runner, (mapping, other) => {
          const defaultExport = (mapping === null && token.hash() === hashes.default);
          console.warn('handled export', other, mapping, 'default?', defaultExport, 'default hoist?', defaultExportHoist);
          // TODO: otherwise unhandled right now
//            commentRange(buffer.slice(start, token.at()));
        });
        return;
      }
      importHandler(runner, (mapping, other) => {
        const {resolved, bundled} = queue(f, other, mapping);

        commentRange(buffer.slice(start, token.at()));

        for (const cand in mapping) {
          // e.g., "x as bar", declare "bar" so it doesn't look like a global
          top.declare(cand);

          // indicate that "bar" points to another file with another name (could be "*")
          refs[cand] = {from: resolved, name: mapping[cand]};

          // the "*" export isn't requested unless we ask for it
          if (mapping[cand] === '*' && !globsFor.has(resolved)) {
            globsFor.set(resolved, cand);
          }
        }
      });

    };

    const stack = (special) => {
      if (special) {
        const next = new Scope(scope);
        if (special & specials.top) {
          tops.unshift(scopes.length);
          top = next;
        }
        scopes.unshift(next);
        scope = next;
        return;
      }

      const previous = scope;
      scopes.shift();
      scope = scopes[0];
      scope.consume(previous);

      if (tops[0] === scopes.length) {
        tops.shift();
        top = scopes[tops[0]];
      }
    };

    // Parse and mark externals. We can do this immediately, and there's no race conditions about
    // which file's globals "wins": the union of globals must be treated as global.
    runner.run(callback, stack);
    top.external(toplevels);

    // FIXME: for now we basically say everything top-level is exported
    // nb. Exports is actually inverse; it is "exported name" => "var name".
    const exports = {};
    for (const cand in top.vars) {
      const {decl} = top.vars[cand];
      if (decl) {
        exports[cand] = cand;
      }
    }
    fileData.set(f, {top, buffer, refs, exports});
  }

  // Reverse the order, so we emit the last module imported first (hoisting woop).
  const output = Array.from(fileData.values()).reverse();

  // Create fake data for all external deps.
  for (const [dep, exports] of externals) {
    const top = new Scope(null);
    for (const key in exports) {
      top.declare(key);
    }
    fileData.set(dep, {exports, top});
  }

  // Pass #2: Prepare all outputs by filling up the top-level namespace. Finds needed renames so
  // we don't clobber global use by bundled files. Update named exports to point to their actual
  // top-level generated names.
  for (const [f, info] of fileData) {
    const {exports, top} = info;

    // If someone wants the glob for this file, pretend that the "*" variable exists so it can be
    // renamed to something valid.
    const nameForGlob = globsFor.get(f);
    if (nameForGlob !== undefined) {
      const safe = top.declare(nameForGlob, true);
      exports['*'] = safe;
    }

    const renames = top.prepare(toplevels);
    for (const key in exports) {
      const cand = exports[key];
      if (cand in renames) {
        // e.g. Instead of "x" exporting "x", maybe it now exports "x$1".
        exports[key] = renames[cand];
      }
    }

    info.renames = renames;
  }

  for (const dep of externals.keys()) {
    const {exports} = fileData.get(dep);
    readable.push(`${rebuildModuleDeclaration(false, exports, dep)};\n`);
  }

  // Pass #3: Actually rename vars and stream output.
  for (const info of output) {
    const {refs, buffer, renames, top, exports} = info;

    // Emit an object-like for the glob for this file.
    if ('*' in exports) {
      readable.push(`const ${exports['*']} = ${rebuildGlobExports(exports)};\n`);
    }

    for (const cand in refs) {
      // This variable actually refers to something else we know about. Insert it into renames.
      const {from, name} = refs[cand];

      const {exports} = fileData.get(from);
      if (!(name in exports)) {
        throw new TypeError(`module ${from} has no export: ${name}`);
      }

      // We want "x" from some other file. Find out what it's called now.
      renames[cand] = exports[name];
    }

    const updates = top.updatesFor(renames);
    updates.sort(({at: a}, {at: b}) => a - b);

    let at = 0;
    for (const next of updates) {
      readable.push(buffer.subarray(at, next.at));
      readable.push(next.update);
      at = next.at + next.length;
    }
    readable.push(buffer.subarray(at));

    if (buffer[buffer.length - 1] !== 10) {
      readable.push('\n');  // force newlines
    }
  }

  readable.push(null);
  return readable;
}


function rebuildGlobExports(exports) {
  const validExport = (key) => key && key !== '*';
  const inner = Object.keys(exports).filter(validExport).map((key) => {
    return `  get ${key}() { return ${exports[key]}; },\n`;
  }).join('');
  return `Object.freeze({\n${inner}})`;
}


/**
 * Builds a module import/export statement.
 *
 * @param {boolean} isExport whether this is an export
 * @param {!Object<string, string>} mapping
 * @param {?string=} target imported from
 */
function rebuildModuleDeclaration(isExport, mapping, target=null) {
  const parts = [];
  let defaultMapping = '';

  // "import foo from './bar.js';" is ok, "export foo from './bar.js';" is not
  if (!isExport) {
    for (const key in mapping) {
      if (mapping[key] === 'default') {
        parts.push(key);
        defaultMapping = key;
        break;
      }
    }
  }

  const grouped = [];
  for (const key in mapping) {
    if (key === defaultMapping) {
      continue;
    }
    const v = mapping[key];
    const target = (key === '*' ? parts : grouped);
    target.push(v === key ? v : `${key} as ${v}`);
  }

  if (grouped.length) {
    parts.push(`{${grouped.join(', ')}}`);
  }

  const out = [isExport ? 'export' : 'import'];

  if (target === null && !parts.length) {
    parts.push('{}');  // no target, include an empty dict so it's valid...-ish
  }
  parts.length && out.push(parts.join(', '));

  if (target !== null) {
    parts.length && out.push('from');
    out.push(JSON.stringify(target));
  }
  return out.join(' ');
}


/**
 * Builds a method which bundles.
 *
 * @return {function(string): !ReadableStream}
 */
export default async function bundler() {
  const source = path.join(path.dirname(import.meta.url.split(':')[1]), 'runner.wasm');
  const wasm = fs.readFileSync(source);

  const runner = await build(wasm);
  return (files) => {
    try {
      return internalBundle(runner, files);
    } catch (e) {
      // FIXME: we get _read() errors?!
      console.error(e);
      throw e;
    }
  }
}


(async function run() {
  const r = await bundler();
  const s = r(process.argv.slice(2));
  s.pipe(process.stdout, {end: false});
  s.resume();
  await new Promise((r) => s.on('close', r));
}());
