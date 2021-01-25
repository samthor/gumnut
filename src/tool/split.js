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
 * @fileoverview Splits an incoming JS file into parts.
 */

import * as blep from '../harness/types/index.js';

import {StackTree, ModuleNode} from './lib/vars.js';
import {stacks, specials, types} from '../harness/harness.js';
import * as lit from '../tokens/lit.js';
import {parseModule} from './lib/handlers.js';
import {hashBuffer} from './lib/hash.js';


const encoder = new TextEncoder();


// TODO(samthor): There's several types of code:
//  - function statements
//  - class statements
//  - top-level imperative code
//  - export containing function/class.
//
// We should parse them separately (?) to generate a graph of relationships. When we see any top-
// level declarations, do something (?) and store them for later.


/**
 * Performs mechanical processing of the passed file for use in module-land by splitting into
 * chunks. This takes ownership of the the passed buffer.
 *
 * @param {blep.Harness} runner
 * @param {!Buffer} buffer
 */
export function processFile(runner, buffer) {
  const {token} = runner;
  runner.prepare(buffer.length).set(buffer);

  const st = new StackTree();
  const node = new ModuleNode();

  let sideEffects = false;

  let topHoist = null;
  const declarations = {};

  const stackHandler = (type) => {
    switch (type) {
      case stacks.null: {
        st.leave();
        return true;
      }

      case stacks.export: {
        // nb. this is a "export var x = .." or "export default <expr>" so we ignore it
        // Notably, either (a) the symbols are already marked as external, or (b) it's a default
        // expression so we can't use it from within this file anyway.
        break;
      }

      case stacks.module: {
        const tokens = [];

        runner.push(() => {
          tokens.push({
            special: token.special(),
            type: token.type(),
            string: token.string(),
          });
        });
        st.enter();
        st.cleanup(() => {
          runner.pop();
          const {mapping, isImport, from} = parseModule(tokens);
          (isImport ? node.importHandler : node.exportHandler)(mapping, from);

          // Because we don't handle tokens normally during module parsing, declare all of the
          // values so they're not seen as globals.
          if (isImport) {
            for (const cand in mapping) {
              st.root.declareImport(cand);
            }
          }

        });
        return true;
      }

      case stacks.function: {
        if (st.depth !== 0) {
          break;
        }

        topHoist = {
          name: null,
          at: token.at(),
        };
        runner.push(() => {
          if (token.type() === types.symbol) {
            topHoist.name = token.string();
            runner.pop();
          }
          defaultCallbackHandler();
        });

        break;
      }

      case stacks.inner: {
        // function which creates var scope
        st.scope(true);

        if (topHoist) {
          const localHoist = topHoist;
          topHoist = null;
          st.cleanup(() => {
            const scope = st.get();
            if (localHoist.name in declarations) {
              declarations[localHoist.name] = null;
            } else {
              declarations[localHoist.name] = {
                externals: scope.externals(),
                from: localHoist.at,
                to: token.void(),
              };
            }
            topHoist = null;
          });
        }

        return true;
      }

      case stacks.block:
      case stacks.control: {
        // this can only be used for imperative code
        if (st.depth === 0) {
          sideEffects = true;
        }
        // something that creates let/const scope
        st.scope();
        return true;
      }

      case stacks.label:
      case stacks.misc:
      case stacks.declare: {
        if (st.depth === 0) {
          sideEffects = true;
        }
        break;
      }

      case stacks.expr: {
        // don't handle top-level expr (fires for `class extends`, `export default`)
        if (st.depth === 0) {
          sideEffects = true;
        }
        break;
      }
    }

    st.enter();  // random scope which we don't care about
    return true;
  };

  const defaultCallbackHandler = () => {
    if (token.type() !== types.symbol) {
      return;
    }

    const cand = token.string();
    const special = token.special();

    if (special & specials.declare) {
      const where = (special & specials.top) ? st.top() : st.get();
      where.declare({cand, at: token.at()});

      if (st.isRoot) {
        const isExternal = (special & specials.external);
        (isExternal ? node.declareExport : node.declare)(cand);
      }
    } else {
      const change = Boolean(special & specials.change);
      st.get().use({cand, at: token.at(), change});
    }
  };

  // Parse and mark externals. We can do this immediately, and there's no race conditions about
  // which file's globals "wins": the union of globals must be treated as global.
  runner.run(defaultCallbackHandler, stackHandler);

  const {root} = st;
  const {externals: globals, locals} = root.split();

  // Files that import others have side-effects, for now.
  const {deps} = node._export();
  for (const key in deps) {
    sideEffects = true;
    break;
  }

  // FIXME: Some debbuging info
  // console.debug('globals:', Array.from(globals));
  // console.debug('locals :', Object.keys(locals));
  // console.debug('defn   :', Object.keys(declarations));
  // console.debug(node._export());

  for (const name of Object.keys(declarations)) {
    const o = declarations[name];
    const {from, to, externals} = declarations[name];

    // Remove actual globals from this chunk, we don't want to import them.
    globals.forEach((global) => externals.delete(global));

    // For now, only operate if we have no dependencies aside globals.
    if (externals.size) {
      delete declarations[name];
      continue;
    }

    // Filter functions that can be changed at runtime.
    const usage = locals[name];
    if (usage.change) {
      delete declarations[name];
      continue;
    }

    // FIXME: this is a poor hash since we're likely rewriting this anyway
    o.hash = hashBuffer(buffer.slice(from, to));


    // const raw = buffer.slice(from, to);

    // console.warn(`// ${o.hash}`);
    // console.warn('export', raw.toString('utf-8'));

    // console.warn('----');
  }

  return {

    sideEffects,

    declarations() {
      const out = {};

      for (const name in declarations) {
        const {hash} = declarations[name];
        if (hash in out) {
          throw new TypeError(`got dup export: ${name}`);
        }
        out[hash] = name;
      }

      return out;
    },

    get(name) {
    },

    replace(name, text='') {
      if (!(name in declarations)) {
        throw new TypeError(`not in declarations: ${name}`);
      }
      const out = declarations[name];
      delete declarations[name];

      // TODO(samthor): we can remove this section anyway (use 'writes' idea)

      const {from, to} = out;

      const slice = buffer.subarray(from, to);
      const copy = Buffer.from(slice);

      slice.fill(32);
      slice[0] = 47;
      slice[1] = 42;

      slice.set(encoder.encode(text), 3);

      slice[slice.length - 2] = 42;
      slice[slice.length - 1] = 47;

      return copy;
    },

    render() {
      return buffer.toString('utf-8');
    }

  };

  // console.warn('initial imports', imports);
  // for (const cand in imports) {
  //   externals.delete(cand);
  // }

  // for (const cand in locals) {
  //   const c = cand || 'default';
  //   if (toplevel[c] === undefined) {
  //     toplevel[c] = cand;
  //   } else {
  //     toplevel['&' + c] = cand;
  //   }
  // }

  // console.debug('externals=', externals);
  // console.debug('imports=', imports, 'toplevel=', toplevel, 'deps=', deps);

  // const refs = {};
  // for (const key in locals) {
  //   refs[key] = 0;
  // }

  // const primaryChunk = {
  //   id: 0,
  //   refs: {},
  //   render(write) {
  //     write('// TODO\n');
  //   },
  // };
  // const chunks = [primaryChunk];

  // for (const name in declarations) {
  //   const o = declarations[name];
  //   if (o === null) {
  //     continue;  // catch dup
  //   }
  //   const {externals: declarationExternals} = o;
  //   externals.forEach((e) => declarationExternals.delete(e));

  //   const id = chunks.length;
  //   refs[name] = id;

  //   const chunkRefs = {};
  //   declarationExternals.forEach((e) => chunkRefs[e] = -1);

  //   chunks.push({
  //     id,
  //     refs: chunkRefs,

  //     render(write) {
  //       write(buffer.slice(o.from, o.to));
  //     },

  //   });
  // }

  // for (const chunk of chunks) {
  //   for (const e in chunk.refs) {
  //     chunk.refs[e] = refs[e];
  //   }
  // }

  // return {
  //   refs,
  //   externals,
  //   chunks,
  // };
}
