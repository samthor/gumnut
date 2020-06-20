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


import {specials, types, hashes} from '../wasm/wrap.js';


class Scope {
  constructor() {
    this.vars = {};
  }

  split() {
    const externals = new Set();
    const locals = {};

    for (const key in this.vars) {
      const data = this.vars[key];
      if (data.decl) {
        locals[key] = data;
      } else {
        externals.add(key);
      }
    }

    return {externals, locals};
  }

  /**
   * Consume a previous child scope.
   *
   * @param {!Scope} child not usable after this call
   */
  consume(child) {
    for (const cand in child.vars) {
      const od = child.vars[cand];
      if (od.decl) {
        continue;  // child scope declared its own var
      }

      const d = this.vars[cand];
      if (d === undefined) {
        this.vars[cand] = {decl: false, at: od.at, length: od.length};
      } else {
        d.at = d.at.concat(od.at);
      }
    }
  }

  /**
   * Mark a variable as being used, with it being an optional declaration.
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
    } else {
      // nb. This could be conditional, by not marking already known decls in non-toplevel scopes.
      // These will never be changed, so the locations eventually get thrown away.
      if (decl) {
        d.decl = true;
      }
      d.at.push(at);
    }
  }

  /**
   * Marks a variable as being silently used. Must happen before delclareIfExists.
   *
   * @param {string} cand
   */
  declareSilentUse(cand) {
    const d = this.vars[cand];
    if (d === undefined) {
      this.vars[cand] = {decl: false, at: [], length: 0};
    }
  }

  /**
   * Marks a variable as a declaration, if it's used anywhere in this file.
   *
   * @param {string} cand
   * @return {boolean} true if it existed
   */
  declareIfExists(cand) {
    const d = this.vars[cand];
    if (d === undefined) {
      return false;
    }
    d.decl = true;
    return true;
  }

  /**
   * Declares the zero variable (i.e., commonly used as the default export) at
   * the passed position.
   *
   * @param {number} at
   */
  declareZeroAt(at) {
    let d = this.vars[''];
    if (d === undefined) {
      this.vars[''] = {decl: true, at: [at], length: 0};
    } else {
      d.decl = true;
      d.at.push(at);
    }
  }
}



/**
 * Performs mechanical processing of the passed file for use in module-land. The returned buffer
 * will contain no static import/export statements.
 *
 * @param {?} runner
 * @param {!Buffer} buffer
 */
export function processFile(runner, buffer) {
  const {token} = runner;
  runner.prepare(buffer.length).set(buffer);

  const root = new Scope();
  const scopes = [root];
  const tops = [0];
  let top = scopes[0];
  let scope = scopes[0];

  const deps = new Set();

  let exportAllStarCount = 0;
  const imports = {};
  const exports = {};

  const importCallback = (start, mapping, other) => {
    deps.add(other);
    commentRange(buffer.slice(start, token.at()));

    for (const cand in mapping) {
      imports[cand] = {from: other, name: mapping[cand]};
    }
  };

  const exportCallback = (start, defaultExportHoist, mapping, other) => {
    const defaultExport = defaultExportHoist || (mapping === null && token.hash() === hashes.default);
    commentRange(buffer.slice(start, token.at()));

    // This is a re-export: something like "export {a} from './other.js'".
    if (other !== null) {
      deps.add(other);
      for (const cand in mapping) {
        if (cand !== '*') {
          // We import _as_ the exported name, as it's guaranteed not to be a duplicate.
          // Note that "mapping[cand]" might still be "*", e.g., "* as foo".
          imports[':' + cand] = {from: other, name: mapping[cand]};
          exports[cand] = ':' + cand;
          continue;
        }

        // Mark unnamed glob re-exports with a number so they're ordered and don't clobber each
        // each other, as they're (unfortunately) quite special.
        const key = `*${++exportAllStarCount}`;
        imports[key] = {from: other, name: '*'};
        exports[key] = `*${other}`;
      }
      return;
    }

    // This is a normal dictionary-like export: e.g., "export {a, bar as zing}"
    if (mapping !== null) {
      for (const cand in mapping) {
        exports[cand] = mapping[cand];
      }
      return;
    }

    // Looks like "export <class|function>". The symbol is annotated properly.
    if (!defaultExport) {
      return;
    }

    // This is an expression like "export default 123" or "export default 123 + 456". We insert a
    // a fake "const" here and claim that it's a symbol. The blank name will be renamed later.
    if (!defaultExportHoist) {
      // "default" => "const ="
      const update = new Uint8Array([99, 111, 110, 115, 116, 32, 61]);
      buffer.set(update, token.at());
      root.declareZeroAt(token.at() + 5);  // place after "const
      exports['default'] = '';
      return;
    }

    // This is "export default <class|function>", and the function/class is annotated properly.
    // Extend the comment range to hide "export default".
    if (defaultExport) {
      const end = token.at() + token.length() - 1;
      commentRange(buffer.slice(start, end));
      buffer[end] = 59;  // insert trailing ";" for safety
      return;
    }

    throw new Error(`unhandled export type`);
  };

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

      // Special-case top-level declarations (these are part of import/export but report here).
      if (special & specials.external) {
        exports[cand] = cand;
      } else if (special & specials.defaultHoist) {
        exports['default'] = cand;  // can be blank string, e.g. "export default function() {}"
      }

    } else if (token.type() === types.symbol) {
      scope.mark(token, false);
    }

    if (scope !== root || token.type() !== types.keyword) {
      return;
    }

    const h = token.hash();
    const start = token.at();
    const defaultExportHoist = Boolean(special & specials.defaultHoist);

    switch (h) {
      case hashes.import:
        importHandler(runner, importCallback.bind(null, start));
        break;

      case hashes.export:
        exportHandler(runner, exportCallback.bind(null, start, defaultExportHoist));
        break;
    }
  };

  const stack = (special) => {
    if (special) {
      const next = new Scope();
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

  // Look for variables which we only export. Technically the only way these are allowed is if they
  // are also imported (can't export e.g., a global or whatever).
  for (const key in exports) {
    top.declareSilentUse(exports[key]);
  }

  // Look for disused imports and remove them. This doesn't remove the import itself, as it might
  // be included for its side-effects. (There's some simple checks for side-effects if we cared.)
  for (const key in imports) {
    if (top.declareIfExists(key)) {
      continue;  // exists, hooray!
    } else if (key[0] === '*') {
      continue;
    }
    delete imports[key];
  }

  const {externals, locals} = root.split();

  const render = (write, renamer) => {
    let writes = [];

    for (const key in locals) {
      const update = renamer(key);
      if (update === undefined || update === key) {
        continue;
      }

      const {at, length} = locals[key];
      writes = writes.concat(at.map((at) => ({at, length, update})))
    }

    writes.sort(({at: a}, {at: b}) => a - b);

    let progress = 0;
    for (const next of writes) {
      let {update, at, length} = next;

      write(buffer.subarray(progress, at));

      // Make sure a zero-length source has padding.
      // TODO(samthor): The parser could emit this such that the empty node always needs a space
      // before itself, so the right side is always safe (e.g. "class^ z", "function^()").
      if (length === 0) {
        if (buffer[at - 1] !== 32) {
          update = ' ' + update;
        }
        if (buffer[at] !== 32 && buffer[at] !== 40) {
          update = update + ' ';
        }
      }

      write(update);
      progress = at + length;
    }
    write(buffer.subarray(progress));  // push tail

    if (buffer[buffer.length - 1] !== 10) {
      write('\n');  // force newlines
    }
  }

  return {
    deps,
    imports,
    exports,
    externals,
    render,
  };
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
