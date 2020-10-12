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


function keyFor(name, f) {
  return `${name}~${f}`;
}


function noop() {}


/**
 * Registry for managing global names within a single bundle.
 */
export class Registry {
  #map = {};
  #globals = new Set(['', 'default', '*']);

  /**
   * Register a variable from a specific file in the global namespace. Returns the updated name, or
   * any previously chosen name for this pair.
   *
   * @param {string} name
   * @param {string} f
   * @return {string}
   */
  register(name, f) {
    const key = keyFor(name, f);

    const prev = this.#map[key];
    if (prev !== undefined) {
      return prev;
    }
    const update = this.name(name);
    this.#map[key] = update;
    // console.warn(`...>>> update '${key}' to '${update}'`);
    return update;
  }

  /**
   * Mark a variable from a specific file as actually referencing a global. It won't be renamed.
   *
   * @param {string} name
   * @param {string} f
   */
  global(name, f) {
    this.#globals.add(name);

    // This deals with a rare and possibly invalid case. We need to record that this file's use of
    // this global should always be renamed to itself. This hits us when a user tries to export a
    // named global (invalid but we allow it).
    const key = keyFor(name, f);
    this.#map[key] = name;
  }

  /**
   * Mark a variable in the global namespace, possibly updating to do so.
   *
   * @param {string} name
   * @return {string} updated name
   */
  name(name) {
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
  
    while (this.#globals.has(update)) {
      update = `${base}$${++count}`;
    }
    this.#globals.add(update);
    return update;
  }
}


/**
 * Helper to parse a JS file and its variable use throughout scopes.
 */
export class Scope {

  /**
   * @param {number} type
   */
  constructor(type) {
    /** @type {!Object<string, {decl: boolean, change: boolean, at: number[]}>} */
    this.vars = {};
    this.type = type;
    this.depth = 1;  // keeps track of if this itself should pop

    /** @type {string|null} */
    this.name = null;
  }

  /**
   * @return {{externals: !Set<string>, locals: !Object<string, string>}}
   */
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
   * @return {!Set<string>}
   */
  externals() {
    const {externals} = this.split();
    return externals;
  }

  /**
   * Consume a previous child scope, gaining any variables not declared within that scope.
   *
   * @param {!Scope} child not usable after this call
   */
  consume(child) {
    for (const cand in child.vars) {
      const od = child.vars[cand];
      if (od.decl) {
        continue;  // child scope declared its own var
      }
      delete child.vars[cand];

      const d = this.vars[cand];
      if (d === undefined) {
        this.vars[cand] = od;
      } else {
        d.at = d.at.concat(od.at);
        d.change = d.change || od.change;
      }
    }
  }

  /**
   * Marks a variable as being declared, but not at a specific location.
   *
   * @param {string} cand
   */
  declareImport(cand) {
    let d = this.vars[cand];
    if (d === undefined) {
      this.vars[cand] = {decl: true, change: false, at: []};
    } else {
      // It's possible to "var x" twice, even without setting any value. For now we just assume
      // that two declarations of the same thing make it mutable, since it's probably a mistake.
      d.change = d.change || d.decl;
      d.decl = true;
    }
  }

  /**
   * Marks a variable as being declared for the first time.
   *
   * @param {{cand: string, at: number}}
   */
  declare({cand, at}) {
    this.declareImport(cand);
    this.vars[cand].at.push(at);
  }

  /**
   * Marks a variable as being used, with it being optionally changed.
   *
   * @param {{cand: string, at: number, change: boolean}}
   */
  use({cand, at, change = false}) {
    let d = this.vars[cand];
    if (d === undefined) {
      this.vars[cand] = {decl: false, at: [at], change: false};
    } else {
      // nb. This could be conditional, by not marking already known decls in non-toplevel scopes.
      // These will never be changed, so the locations eventually get thrown away.
      d.change = d.change || change;
      d.at.push(at);
    }
  }
}


export class StackTree {
  #root = new Scope(0);
  #scopes = [this.#root];
  #tops = [0];
  #top = this.#root;
  #scope = this.#root;
  #cleanup = [noop];

  get depth() {
    return this.#scopes.length - 1;
  }

  get root() {
    return this.#root;
  }

  get isRoot() {
    return this.#root === this.#top;
  }

  top() {
    return this.#top;
  }

  get() {
    return this.#scope;
  }

  scope(root = false) {
    this.#cleanup.push(noop);
    const next = new Scope(0);

    if (root) {
      // Creates top-level (var) scope, such as a function.
      this.#tops.unshift(this.#scopes.length);
      this.#top = next;
    }

    this.#scopes.unshift(next);
    this.#scope = next;

    return next;
  }

  enter() {
    ++this.#scope.depth;
    this.#cleanup.push(noop);
  }

  cleanup(handler) {
    const last = this.#cleanup.pop();
    this.#cleanup.push(() => (last(), handler()));
  }

  /**
   * @return {boolean} if a scope was removed
   */
  leave() {
    this.#cleanup.pop()();
    if (--this.#scope.depth) {
      return false;
    }

    const previous = this.#scope;
    this.#scopes.shift();
    this.#scope = this.#scopes[0];
    this.#scope.consume(previous);

    if (this.#tops[0] === this.#scopes.length) {
      this.#tops.shift();
      this.#top = this.#scopes[this.#tops[0]];
    }

    return true;
  }

}


/**
 * Applied to all top-level symbols, including both:
 *   - ones which are not explicitly supported
 *   - ones which are local and explicitly supported (for module split)
 *
 * This only appears inside the keys of #symbols below.
 */
export const localPrefix = '^';


/**
 * Applied to re-exports that come from another file. Creates locally invalid versions that we can
 * later re-export.
 *
 * This only appears inside the keys of #imports and values of #symbols below.
 */
export const externalPrefix = ':';


export class ModuleNode {

  /** @type {!Object<string, {from: string, name: string}>} */
  #imports = {};

  /** @type {!Object<string, {from: string|null, name: string}>} */
  #exports = {};

  /** @type {!Set<string>} */
  #symbols = new Set();

  /** @type {!Object<string, boolean>} */
  #deps = {};

  /**
   * Merges an import into the toplevel of this node.
   *
   * This models a normal import statement: `import {x} from 'foo';`
   *
   * @param {!Object<string, string>} mapping
   * @param {string} from
   */
  importHandler = (mapping, from) => {
    this.#deps[from] = this.#deps[from] ?? false;

    for (const cand in mapping) {
      this.#imports[cand] = {from, name: mapping[cand]};
      this.#symbols.add(cand);
    }
  };

  /**
   * Merges an export into the toplevel of this node. Can be a re-export (valid import from), or
   * just a local export (null from).
   *
   * @param {!Object<string, string>} mapping
   * @param {string|null} from
   */
  exportHandler = (mapping, from = null) => {
    if (from === null) {
      for (const cand in mapping) {
        this.#exports[cand] = {from: null, name: mapping[cand]}
      }
      return;
    }

    this.#deps[from] = this.#deps[from] ?? false;

    // This is a re-export: none of these variables ever exist in this node, we're just passing
    // them through for our users.

    for (const cand in mapping) {
      if (cand !== '*') {
        this.#exports[cand] = {from, name: mapping[cand]};
        continue;
      }
      if (mapping[cand] !== '*') {
        throw new TypeError(`can't export ${mapping[cand]} as *`);
      }
      this.#deps[from] = true;
    }
  };

  declare = (cand) => {
    this.#symbols.add(cand);
  };

  declareExport = (cand) => {
    // Even though we're exporting this (e.g. `export var x = 123`), allow it to be accessed via
    // its local mapping too.
    this.#symbols.add(cand);
    this.#exports[cand] = {from: null, name: cand};
  };

  _export() {
    return {imports: this.#imports, exports: this.#exports, symbols: this.#symbols, deps: this.#deps};
  }
}

