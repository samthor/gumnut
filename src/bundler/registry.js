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
 * Registry for managing global names across a bundle.
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