
import * as lit from '../../tokens/lit.js';
import {types} from '../../harness/harness.js';


/**
 * @param {!Array<Object>} t
 * @param {!Object<string, string>} mapping
 */
function internalConsumeModuleDeep(t, mapping) {
  while (t[0].type !== types.close) {
    if (t[0].special === lit.$COMMA) {
      t.shift();
      continue;
    }

    const external = t[0].string;
    let target = t[0].string;
    t.shift();
    if (t[0].special === lit.AS) {
      t.shift();
      ({string: target} = t.shift());
    }

    mapping[target] = external;
  }
}


/**
 * Parse tokens from a module import or export.
 *
 * @param {!Array<Object>} tokens
 * @return {{mapping: !Object<string, string>, isImport: boolean, from: string|null}}
 */
export function parseModule(tokens) {
  const t = tokens.slice();
  const start = t.shift();
  const isImport = start.special === lit.IMPORT;
  const mapping = {};

  if (t[t.length - 1].type === types.semicolon) {
    t.pop();
  }

  while (t.length && t[0].special !== lit.FROM) {
    if (t[0].special === lit.$COMMA) {
      t.shift();
      continue;
    }

    if (t[0].special === lit.$STAR) {
      t.shift();
      if (t[0].special === lit.AS) {
        t.shift();
        const {string} = t.shift();
        mapping[string] = '*';
      } else {
        mapping['*'] = '*';
      }
      continue;
    }

    if (isImport && t[0].type === types.symbol) {
      mapping[t[0].string] = 'default';
      t.shift();
      continue;
    }

    if (t[0].type === types.brace) {
      t.shift();  // must be {
      internalConsumeModuleDeep(t, mapping);
      t.shift();  // must be }
      continue;
    }

    throw new TypeError(`unexpected in parseModule`);
  }

  const from = t.length ? eval(t[1].string) : null;  // FIXME: don't eval
  return {mapping, isImport, from};
}