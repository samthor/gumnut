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


const globExport = '__';


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

  declared(cand) {
    return this.vars[cand]?.decl || false;
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
        // There's two cases here: when "x" is eventually bundled, we prefer using its name.
        // If "x" is left as external, then we prefer "y".
        // TODO: Could we use a totally invalid name (e.g. ":foo") and always prefer the left? use e.g. ":x"?
        toplevels.add(cand);
        continue;
      }

      const isEmpty = (cand.length === 0);
      let prefix = cand;
      if (cand === '') {
        prefix = '__default';
      }

      // Try to rename this top-level var. It's possible but odd that "foo$1" is already defined
      // (maybe output from another bundler), so increment until we find an empty slot.
      let update;
      for (let index = 0; ++index; ) {
        update = `${prefix}$${index}`;
        if (!toplevels.has(update)) {
          break;
        }
      }
      toplevels.add(update);

      // If the candidate is the empty string (unnamed function statement in export default), then
      // it needs to be padded by spaces. This can result in slightly odd spacing but whatever.
      if (isEmpty) {
        update = ` ${update} `;
      }
      renames[cand] = update;
    }

    return renames;
  }

  updatesFor(renames) {
    let updates = [];

    for (const cand in this.vars) {
      const {decl, length, at} = this.vars[cand];

      // Not renamed. Skip.
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
   * @param {boolean=} safe whether to rename to always be new
   * @return {string} updated variable name (if safe)
   */
  declare(cand, safe=false) {
    if (safe) {
      let update = cand;
      for (let index = 0; ++index; ) {
        if (!(update in this.vars)) {
          break;
        }
        update = `${cand}$${index}`;
      }
      cand = update;
    }

    let d = this.vars[cand];
    if (d === undefined) {
      // FIXME: we need to recalc length
      const te = new TextEncoder('utf-8');
      const buf = te.encode(cand);
      this.vars[cand] = {decl: true, at: [], length: buf.length};
    } else {
      if (safe) {
        throw new Error(`didn't find safe var: ${cand}`);
      }
      d.decl = true;
    }

    return cand;
  }
}


/**
 * 
 */
function internalBundle(runner, files) {
  const {token} = runner;
  const readable = new stream.Readable({emitClose: true});

  const toplevels = new Set(['', '*']);  // specials that cannot really exist
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
 * Fills the passed view with a comment and the same number of newlines as the source.
 *
 * @param {!Uint8Array} view
 */
function commentRange(view) {
  let target = view.length;
  view.forEach((c) => {
    if (c === 10) {
      --target;
    }
  });

  if (view.length < 4) {
    // Just fill with spaces. This range is too tiny.
    view.fill(32, 0, target);
  } else {
    // Fill with a comment containing dots.
    view[0] = 47;
    view[1] = 42;
    view.fill(46, 2, target - 2);
    view[target - 2] = 42;
    view[target - 1] = 47;
  }

  while (target < view.length) {
    view[target++] = 10;
  }
}


/**
 * Adds an import handler. Assumes token currently points to "import" keyword.
 */
function importHandler(runner, callback) {
  const mapping = {};
  let pendingSource = '';

  const {token} = runner;

  runner.push((special) => {

    if (special & specials.modulePath) {
      // FIXME: avoid eval
      const other = eval(token.string());
      runner.pop();
      runner.push((special) => {
        // Push this handler so we can trigger callback _after_ the modulePath.
        const prev = runner.pop();
        callback(mapping, other);
        prev(special);
      })
      return;
    }

    if (token.hash() === hashes._star) {
      pendingSource = '*';
      return;
    }

    if (token.hash() === hashes._comma || token.type() == types.close) {
      // TODO: needed in import-only?
      pendingSource = '';
      return;
    }

    if (special & specials.external) {
      pendingSource = token.string();
    }

    if (special & specials.declare) {
      mapping[token.string()] = pendingSource || 'default';
      pendingSource = '';
    }

  });
}


/**
 * Adds an export handler. Assumes token currently points to "export" keyword. Returns early if
 * this is actually an export an expr-like thing (or var, function, class etc).
 */
function exportHandler(runner, callback) {
  const mapping = {};
  let pendingSource = '';

  const {token} = runner;

  const tailCallback = (special) => {
    if (token.type() === types.comment) {
      return;  // nb. we consume comments
    }

    if (token.hash() === hashes.from) {
      return;  // ok
    }

    if (special & specials.modulePath) {
      const other = eval(token.string());
      runner.pop();
      runner.push((special) => {
        // Push this handler so we can trigger callback _after_ the modulePath.
        const prev = runner.pop();
        callback(mapping, other);
        prev(special);
      })
      return;
    }

    // Unhandled, so call our parent handler immediately.
    const prev = runner.pop();
    callback(mapping, null);
    prev(special);
  };

  // Cleanup for mapping code.
  const cleanup = () => {
    if (pendingSource) {
      mapping[pendingSource] = pendingSource;
    }
    pendingSource = '';
  };

  // Handles normal mappings: "* as y", "foo as bar, zing, default as woo".
  const mappingCallback = (special) => {
    const type = token.type();
    const hash = token.hash();

    if (type == types.close || (type === types.keyword && hash === hashes.from)) {
      cleanup();
      runner.pop();
      runner.push(tailCallback);
      return;
    }

    if (hash === hashes._star) {
      pendingSource = '*';
      return;
    }

    if (hash === hashes._comma) {
      cleanup();
      return;
    }

    if (special & specials.external) {
      if (pendingSource) {
        mapping[token.string()] = pendingSource;
        pendingSource = '';
      } else {
        pendingSource = token.string();
      }
    }

    if (type === types.symbol) {
      pendingSource = token.string();
    }
  };

  // Push an initial handler to catch '*', '{' or 'default'.
  runner.push((special) => {
    if (token.type() === types.comment) {
      return;  // nb. we consume comments
    }

    const prev = runner.pop();

    if (token.hash() === hashes._star) {
      runner.push(mappingCallback);
      mappingCallback();
    } else if (token.type() == types.brace) {
      runner.push(mappingCallback);
    } else {
      // This is just a regular export of a var, or function etc.
      // It includes "default", so the callback should check.
      callback(null, null);
      prev(special);
    }
  });
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
