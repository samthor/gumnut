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
import { type } from 'os';


class Scope {
  constructor(parent) {
    this.parent = parent;
    this.vars = {};
  }

  _get(cand) {
    let found = this.vars[cand];
    if (found === undefined) {
      this.vars[cand] = found = {decl: false, at: []};
    }
    return found;
  }

  /**
   * Adds this scope's externals to the passed Set. This must happen for all files first, as these
   * cannot be renamed (e.g. "window", "this").
   *
   * @param {!Set<string>} toplevels where to mark global use
   */
  external(toplevels) {
    for (const cand in this.vars) {
      const {decl} = this.vars[cand];
      if (decl === false) {
        toplevels.add(cand);
      }
    }
  }

  /**
   * @param {!Set<string>} toplevels to use and mark
   * @return {!Object<string, string>} renames to apply to this scope
   */
  prepare(toplevels) {
    const renames = {};

    for (const cand in this.vars) {
      const {decl} = this.vars[cand];

      // Not defined at the top-level, so it must be a global. Don't rename it.
      if (decl === false) {
        continue;
      }

      // We can use this variable name at the top-level, it's not already defined.
      if (!toplevels.has(cand)) {
        // TODO: "import {x as y}" will block out "y", even if not used: probably fine?
        toplevels.add(cand);
        continue;
      }

      // Try to rename this top-level var. It's possible but odd that "foo$1" is already defined
      // (maybe output from another bundler), so increment until we find an empty slot.
      let update;
      for (let index = 0; ++index; ) {
        update = `${cand}$${index}`;
        if (!toplevels.has(update)) {
          break;
        }
      }
      toplevels.add(update);
      renames[cand] = update;
    }

    return renames;
  }

  updatesFor(renames) {
    let updates = [];

    for (const cand in this.vars) {
      const {decl, length, at} = this.vars[cand];

      // Skip this rename.
      if (!(cand in renames)) {
        continue;
      } else if (decl === false) {
        throw new Error(`got global rename: ${cand}`);
      }

      const update = renames[cand];
      updates = updates.concat(at.map((at) => ({at, length, update})));
    }

    return updates;
  }

  /**
   * @param {!Set<string>} toplevels that are already being used
   * @param {!Object<string, string>} mappings to update
   * @return {!Array<{at: number, cand: string, update: string}>} unsorted updates
   */
  updates(toplevels, mappings) {
    let updates = [];

    for (const cand in this.vars) {
      const {decl, length, at} = this.vars[cand];

      // Not defined at the top-level, so it must be a global. Don't rename it.
      if (decl === false) {
        continue;
      }

      // We can use this variable name at the top-level, it's not already defined.
      if (!toplevels.has(cand)) {
        toplevels.add(cand);
        continue;
      }

      // Try to rename this top-level var. It's possible but odd that "foo$1" is already defined
      // (maybe output from another bundler), so increment until we find an empty slot.
      let update;
      for (let index = 0; ++index; ) {
        update = `${cand}$${index}`;
        if (!toplevels.has(update)) {
          break;
        }
      }

      // Announce shared module mappings.
      mappings[cand] = update;

      // This includes many locations (note concat) for this var.
      updates = updates.concat(at.map((at) => ({at, length, update})));
    }

    return updates;
  }

  /**
   * Consume a previous child scope.
   *
   * @param {!Scope} child
   */
  consume(child) {
    for (const cand in child.vars) {
      const od = child.vars[cand];
      if (od.decl) {
        continue;
      }

      const d = this._get(cand);
      d.at = d.at.concat(od.at);
    }
  }

  /**
   * Mark a variable as being used, with it being an optional declaration.
   *
   * This only records if we're a declaration, or we're at the top level, or not already declared
   * locally.
   * 
   * @param {!Token} token
   * @param {boolean} decl whether this is a declaration
   */
   mark(token, decl) {
    const cand = token.string();
    const at = token.at();
    let d = this.vars[cand];
    if (d === undefined) {
      this.vars[cand] = {decl, at: [at], length: token.length()};
    } else if (decl || (this.parent === null || !d.decl)) {
      // decl always gets marked (shadows); or
      // top-level use always get marked; or
      // if we weren't yet confidently declared here (maybe hoisted)
      if (decl) {
        d.decl = true;
      }
      d.at.push(at);
    }
  }

  /**
   * Declares a variable, but without actually marking it for update.
   *
   * @param {string} cand
   */
  declare(cand) {
    let d = this.vars[cand];
    if (d === undefined) {
      // FIXME: we need to recalc length
      const te = new TextEncoder('utf-8');
      const buf = te.encode(cand);
      this.vars[cand] = {decl: true, at: [], length: buf.length};
    } else {
      d.decl = true;
    }
  }
}


/**
 * Builds a method which rewrites imports from a passed filename.
 *
 * @return {function(string): !ReadableStream}
 */
export default async function rewriter() {
  const source = path.join(path.dirname(import.meta.url.split(':')[1]), 'runner.wasm');
  const wasm = fs.readFileSync(source);

  const {prepare, run} = await build(wasm);

  return async (files) => {
    const toplevels = new Set();
    let changes = 0;

    const fileQueue = new Set();
    for (const f of files) {
      fileQueue.add(path.resolve(f));
    }
    const fileData = new Map();

    // Pass #1: process all files. Read their imports/exports.
    for (const f of fileQueue) {
      // nb. read here, because we need all buffers at end (can't reuse wasm space)
      const buffer = fs.readFileSync(f);
      const token = prepare(buffer.length, (b) => b.set(buffer));

      const scopes = [new Scope(null)];
      const tops = [0];
      let top = scopes[0];
      let scope = scopes[0];

      const modules = {};
      let importCallback = null;

      const callback = (special) => {
        if (importCallback) {
          return importCallback(special);
        }

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

        if (token.type() === types.keyword) {
          const h = token.hash();
          if (h === hashes.import) {
            // ok, starts import
          } else if (h === hashes.export && (special & specials.external)) {
            // ok, starts export-like-import (reexport)
          } else {
            // TODO: "regular" export
            return;
          }
          const isExport = (h === hashes.export);
          const start = token.at();

          importCallback = buildImportCallback(token, (mapping, to, from) => {
            let bundled = false;
            if (/^\.\.?\//.test(from)) {
              // Make 'from' an absolute path.
              from = path.join(path.dirname(f), from);
              if (fs.existsSync(from)) {
                fileQueue.add(from);
                bundled = true;
              }
            }
            importCallback = null;

            // TODO: for now, just blank out the imports with empty space
            buffer[start + 0] = 47;
            buffer[start + 1] = 42;
            buffer.fill(46, start + 2, to - 2);
            buffer[to - 2] = 42;
            buffer[to - 1] = 47;

            if (isExport) {
              return;  // FIXME
            }

            // Declare the variables just imported (e.g. "import x from foo" => "x" exists now).
            for (const cand in mapping) {
              top.declare(cand);
            }

            // We only want to include modules which actually import things or which aren't known.
            // Bundled modules with side-effects are already implied since they're queued above.
            const def = modules[from] || {};
            Object.assign(def, mapping);
            if (Object.keys(def).length || !bundled) {
              modules[from] = def;
            }
          });
        }

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
      const s = run(callback, stack);
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
      fileData.set(f, {top, buffer, modules, exports});
    }

    // Reverse the order, so we emit the last module imported first (hoisting woop).
    console.warn('globals', toplevels);
    const output = Array.from(fileData.values()).reverse();

    // Pass #2: Prepare all outputs by filling up the top-level namespace. Finds needed renames so
    // we don't globber global use by bundled files. Update named exports to point to their actual
    // top-level generated names.
    for (const info of output) {
      const {exports, top} = info;

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

    // Pass #3: Actually rename vars and stream output.
    for (const f of output) {
      const {modules, buffer, renames, top} = f;

      for (const from in modules) {
        const mapping = modules[from];
        if (!fileData.has(from)) {
          // FIXME: this is an unknown/external file
          continue;
        }
        const {exports} = fileData.get(from);

        for (const name in mapping) {
          const cand = mapping[name];

          if (!(cand in exports)) {
            throw new TypeError(`module ${from} has no export: ${cand}`);
          }

          renames[name] = exports[cand];
        }
      }

      const updates = top.updatesFor(renames);
      updates.sort(({at: a}, {at: b}) => a - b);

      const readable = new stream.Readable({emitClose: true});

      let at = 0;
      for (const next of updates) {
        readable.push(buffer.subarray(at, next.at));
        readable.push(next.update);
        at = next.at + next.length;
        ++changes;
      }
      readable.push(buffer.subarray(at));

      readable.push(null);
      await streamTo(readable);
    }
  };
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
    if (v === '*') {
      parts.push(key === '*' ? '*' : `* as ${key}`);
    } else {
      grouped.push(v === key ? v : `${v} as ${key}`)
    }
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
 * Builds a helper that intercepts import/export statements and builds a mapping statement.
 *
 * Currently only handles import/export that ends with a module path.
 *
 * @param {!Token} token
 * @param {function(!Object<string, string>, number, string): void}
 * @return {function(number): void}
 */
function buildImportCallback(token, done) {
  const isExport = (token.hash() === hashes.export);
  const mapping = {};

  let pendingSource = '';

  const cleanup = () => {
    if (isExport && pendingSource) {
      mapping[pendingSource] = pendingSource;
    }
    pendingSource = '';
  };

  return (special) => {
    if (token.type() === types.comment) {
      // nb. we clobber comments within import/export
      return;
    }

    if (special & specials.modulePath) {
      cleanup();
      // FIXME: avoid eval
      const other = eval(token.string());
      done(mapping, token.at() + token.length(), other);
      return;
    }

    if (token.hash() === hashes._star) {
      pendingSource = '*';
      return;
    }

    if (token.hash() === hashes._comma || token.type() == types.close) {
      cleanup();
      return;
    }

    if (special & specials.external) {
      if (isExport && pendingSource) {
        mapping[token.string()] = pendingSource;
        pendingSource = '';
      } else {
        pendingSource = token.string();
      }
    }

    if (special & specials.declare) {
      mapping[token.string()] = pendingSource || 'default';
      pendingSource = '';
    }
  };
}


function streamTo(s, target = process.stdout) {
  s.pipe(target, {end: false});
  s.resume();
  return new Promise((r) => s.on('close', r));
}


(async function run() {
  const r = await rewriter();

  r(process.argv.slice(2));

}());
