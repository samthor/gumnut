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
import build from './wrap.js';
import path from 'path';
import stream from 'stream';

import {processFile} from '../bundler/file.js';
import {Registry} from '../bundler/registry.js';
import * as rebuild from '../bundler/rebuild.js';


function resolve(other, importer) {
  if (/^\.{0,2}\//.test(other)) {
    return path.join(path.dirname(importer), other);
  }
  return other;
}


const validExport = (key) => key && key[0] !== '*';
const emptySet = Object.freeze(new Set());


function internalBundle(runner, files) {
  const readable = new stream.Readable({emitClose: true});
  const write = readable.push.bind(readable);

  const registry = new Registry();
  const fileData = new Map();
  const unbundledImports = new Map();

  // Part #1: Parse every file and proceed recursively into dependencies. This matches ESM execution
  // order.
  const processed = new Set();
  const process = (f = '') => {
    if (processed.has(f)) {
      return emptySet;
    }
    processed.add(f);

    let buffer;
    if (f.length === 0) {
      // We generate fake source for the top-level file: exports all targets together.
      const source = files.map((f) => `export * from ${JSON.stringify(f)};\n`).join('');
      buffer = Buffer.from(source, 'utf-8');
    } else {
      // This isn't bundled into our output, so mark it as such and return early.
      if (!fs.existsSync(f)) {
        unbundledImports.set(f, {});
        return emptySet;
      }
      buffer = fs.readFileSync(f);
    }

    const {deps, externals, imports, exports, render} = processFile(runner, buffer);

    // Immediately resolve future dependencies.
    const globExports = new Set();
    deps.forEach((globExport, dep) => {
      const resolved = resolve(dep, f);
      const subordinateGlobExport = process(resolved);
      if (globExport) {
        subordinateGlobExport.forEach((v) => globExports.add(v));
        globExports.add(resolved);
      }
    });

    // processFile doesn't know how to resolve external imports; resolve here.
    for (const name in imports) {
      const d = imports[name];
      d.from = resolve(d.from, f);
    }

    // Flatten any run of globExports here.
    // TODO(samthor): This is probably in the wrong order.
    for (const other of globExports) {
      exports[`*${other}`] = '*';
    }

    // Store externals globally, and ensure they're not renamed here.
    const renames = {};
    externals.forEach((external) => {
      registry.global(external, f);
      renames[external] = external;
    });

    fileData.set(f, {imports, exports, render, renames});
    return globExports;
  };

  process();

  // If we want '*' from a source file, then we have to awkwardly ensure that _its_ unbundled
  // exports are correctly identified. e.g.:
  //   - "a.js" requires * from "b.js" which exports all of "other".
  //   - we need to ensure that we import "* as <something>" from "other".
  // This rarely happens in the real world.
  const hoistExportsOf = (f) => {
    const {exports, renames} = fileData.get(f);

    for (const key in exports) {
      if (key[0] !== '*' || key.length === 1) {
        continue;
      }
      const from = key.substr(1);
      const mapping = unbundledImports.get(from);
      if (mapping === undefined) {
        continue;
      }

      mapping['*'] = registry.register('*', from);
      renames[key] = mapping['*'];
    }
  };

  // Part #2: Remap all imports. This needs to be done before exports, as we might require the
  // additional glob export from another bundled file.
  for (const [f, data] of fileData) {
    const {imports, renames} = data;

    for (const x in imports) {
      let {from, name} = imports[x];
      const isBundled = fileData.has(from);

      // If the referenced export comes from within our bundle, then prefer its original name.
      if (isBundled) {
        const {exports: otherExports} = fileData.get(from);
        const real = otherExports[name];
        if (real === undefined) {
          if (name !== '*') {
            throw new Error(`${from} does not export: ${name}`);
          }
          // There's no original name as we're asking for all its values. Just use '*' and the
          // registry will give us a sensible name later.
          if (!('*' in otherExports)) {
            otherExports['*'] = '*';
            hoistExportsOf(from);
          }
        } else {
          name = real;
        }
      }

      const update = registry.register(name, from);
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
    // We can't include a glob-style with regular imports.
    // TODO(samthor): default also has odd rules (can go with either glob or misc).
    if ('*' in mapping) {
      write(`${rebuild.importModuleDeclaration({'*': mapping['*']}, f)};\n`);
      delete mapping['*'];

      // If we import anything else, we need to reimport below.
      let done = true;
      for (const _ in mapping) {
        done = false;
        break;
      }
      if (done) {
        continue;
      }
    }
    write(`${rebuild.importModuleDeclaration(mapping, f)};\n`);
  }

  // Part #4: Remap exports, and rewrite/render module content.
  for (const [f, {render, exports, renames}] of fileData) {
    for (const x in exports) {
      const real = exports[x];
      if (!(real in renames)) {
        renames[real] = registry.register(real, f);
      }
    }
    const toplevel = (f.length === 0);

    // We need to calculate our exports.
    if ('*' in exports || toplevel) {
      const unbundled = {};
      const expandedExports = {};

      for (const key in exports) {
        if (key === '*') {
          continue;  // this is our hint to generate in the first place
        } else if (key[0] !== '*') {
          expandedExports[key] = renames[exports[key]];
          continue;  // this is a boring export
        }

        // Otherwise, this is a re-export of a global.
        const from = key.substr(1);
        const isBundled = fileData.has(from);

        // TODO(samthor): Technically there's probably a prescribed order here. Instead, we're just
        // placing all unbundled keys after the bundled ones.
        if (!isBundled) {
          unbundled[from] = renames[key];
          continue;
        }

        // If not, merge its keys here.
        const {exports: otherExports, renames: otherRenames} = fileData.get(from);
        for (const key in otherExports) {
          if (validExport(key)) {
            expandedExports[key] = otherRenames[otherExports[key]];
          }
        }
      }

      // Someone wants the global exports of this file.
      if ('*' in exports) {
        const name = renames['*'];

        // TODO(samthor): If the underlying value never mutates, we can dispense with the getter.
        // This is pretty easy to determine in the parser with static analysis.
        const inner = Object.keys(expandedExports).map((key) => {
          return `  get ${key}() { return ${expandedExports[key]}; },\n`;
        }).join('');
        readable.push(`const ${name} = /* global export of \"${f}\" */ {\n${inner}};\n`);

        // nb. Other bundlers just use Object.assign here, but that's not actually valid as the
        // values of the bundled external dependency could actually _change_.
        // TODO(samthor): This should probably be a configuration flag.
        for (const extra of Object.values(unbundled)) {
          const s = `for (let _ in ${extra}) {
  Object.defineProperty(${name}, _, {enumerable: true, get() { return ${extra}[_]; }})};
}
`;
          readable.push(s);
        }
        readable.push(`Object.freeze(${name});\n`);
      }

      // This is the top-level entrypoint, so export everything we know about.
      if (toplevel) {
        for (const extra of Object.keys(unbundled)) {
          write(`export * from ${JSON.stringify(extra)};\n`);
        }

        const grouped = [];
        for (const key in expandedExports) {
          const v = expandedExports[key];
          grouped.push(v === key ? v : `${v} as ${key}`);
        }
        if (grouped.length) {
          readable.push(`export {${grouped.join(', ')}};\n`);
        }
      }
    }

    if (toplevel) {
      continue;  // nothing good here
    }

    render(write, (name) => {
      const update = renames[name];
      if (update !== undefined) {
        return update;
      }

      // Otherwise, this is local to this file: just make it unique to our bundle.
      return registry.name(name);
    });
  }

  readable.push(null);
  return readable;
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
