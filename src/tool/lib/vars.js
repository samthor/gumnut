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
   * @param {number} at
   */
  constructor(type, at) {
    /** @type {!Object<string, {decl: boolean, at: number[], length: number}>} */
    this.vars = {};
    this.type = type;
    this.at = at;
    this.depth = 1;  // keeps track of if this itself should pop
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
   * @param {blep.Token} token
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
   * Marks a variable as being silently used. Must happen before declareIfExists.
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
