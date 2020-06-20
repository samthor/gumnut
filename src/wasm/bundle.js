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

  const fileData = new Map();
  const unbundledImports = new Map();

  // Part #1: Parse every file and proceed recursively into dependencies. This matches ESM execution
  // order.
  const processed = new Set();
  const process = (f, toplevel = false) => {
    if (processed.has(f)) {
      return false;
    }
    processed.add(f);

    if (!fs.existsSync(f)) {
      unbundledImports.set(f, {});
      return true;
    }

    const buffer = fs.readFileSync(f);
    const {deps, externals, imports, exports, render} = processFile(runner, buffer);

    // Immediately resolve future dependencies.
    deps.forEach((dep) => process(resolve(dep, f)));

    // Store externals globally, and ensure they're not renamed here.
    const renames = {};
    externals.forEach((external) => {
      globals.add(external);
      renames[external] = external;
    });

    // processFile doesn't know how to resolve external imports; resolve here.
    for (const name in imports) {
      const d = imports[name];
      d.from = resolve(d.from, f);
    }

    fileData.set(f, {imports, exports, render, renames, toplevel});
  };
  for (const f of files) {
    process(path.resolve(f), true);
  }

  // Part #2: Remap all imports. This needs to be done before exports, as we might require the
  // additional glob export from another bundled file.
  for (const [f, data] of fileData) {
    const {imports, renames} = data;

    for (const x in imports) {
      let {from, name} = imports[x];

      // If the referenced export comes from within our bundle, then prefer its original name.
      const isBundled = fileData.has(from);
      if (isBundled) {
        const {exports: otherExports} = fileData.get(from);
        const real = otherExports[name];
        if (real === undefined) {
          if (name !== '*') {
            throw new Error(`${from} does not export: ${name}`);
          }
          otherExports['*'] = '*';
        } else {
          name = real;
        }
      }

      const update = register(name, from);
      renames[x] = update;

      // If the dep is unbundled, we need to record what it's called so we can import it later.
      if (!isBundled) {
        const mapping = unbundledImports.get(from);
        mapping[name] = update;
      }
    }
  }

  // Part #3: Announce all external modules we depend on.
  for (const [f, mapping] of unbundledImports) {
    write(`${rebuildModuleDeclaration(false, mapping, f)}\n`);
  }

  // Part #4: Remap exports, and rewrite/render module content.
  for (const [f, {render, exports, renames, toplevel}] of fileData) {
    for (const x in exports) {
      const real = exports[x];
      if (!(real in renames)) {
        renames[real] = register(x, f);
        console.warn('renaming', real, 'to', renames[real], 'was', x);
      }
    }
    if ('*' in exports) {
      readable.push(`const ${renames['*']} = ${rebuildGlobExports(exports, renames)};\n`);
    }
    if (toplevel) {
      // gross but seems to work (exports are flipped)
      const inverted = {};
      for (const k in exports) {
        inverted[renames[exports[k]] || exports[k]] = k;
      }
      write(`${rebuildModuleDeclaration(true, inverted)};\n`);
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

  readable.push(null);
  return readable;
}


function rebuildGlobExports(exports, renames) {
  const validExport = (key) => key && key !== '*';
  const inner = Object.keys(exports).filter(validExport).map((key) => {
    const v = renames[exports[key]];
    return `  get ${key}() { return ${v}; },\n`;
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
