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
 * @fileoverview Helpers to rebuild a bundled JS file.
 */


/**
 * Creates a glob exports line, i.e., a variable which includes all variables
 * from a specific file.
 *
 * @param {!Object<string, string>} exports
 * @param {!Object<string, string>} renames
 * @param {!Array=} extras to include in this glob (i.e., re-export)
 */
export function globExports(exports, renames, extras=[]) {
  const validExport = (key) => key && key[0] !== '*';
  const inner = Object.keys(exports).filter(validExport).map((key) => {
    const v = renames[exports[key]];
    return `  get ${key}() { return ${v}; },\n`;
  }).join('');
  const o = `{\n${inner}}`;

  if (extras.length) {
    // TODO: what is the correct order?
    return `Object.freeze(Object.assign(${o}, ${extras.join(', ')}))`;
  }
  return `Object.freeze({\n${inner}})`;
}


/**
 * Builds a module export statement.
 *
 * @param {!Object<string, string>} mapping
 * @param {?string=} target re-exported from (optional)
 */
export function exportMappingDeclaration(mapping, target=null) {
  const grouped = [];

  for (const key in mapping) {
    const v = mapping[key];
    grouped.push(v === key ? v : `${v} as ${key}`);
  }

  const out = ['export'];
  out.push(`{${grouped.join(', ')}}`)

  if (target !== null) {
    out.push('from');
    out.push(JSON.stringify(target));
  }

  return out.join(' ');
}


/**
 * Builds a module import statement.
 *
 * @param {!Object<string, string>} mapping
 * @param {string} target imported from
 */
export function importModuleDeclaration(mapping, target) {
  const parts = [];
  let defaultMapping = '';

  for (const key in mapping) {
    if (mapping[key] === 'default') {
      parts.push(key);
      defaultMapping = key;
      break;
    }
  }

  const grouped = [];
  for (const key in mapping) {
    if (key === defaultMapping) {
      continue;
    }
    const v = mapping[key];
    const goal = (key === '*' ? parts : grouped);
    goal.push(v === key ? v : `${key} as ${v}`);
  }

  if (grouped.length) {
    parts.push(`{${grouped.join(', ')}}`);
  }

  const out = ['import'];

  parts.length && out.push(parts.join(', '));
  parts.length && out.push('from');
  out.push(JSON.stringify(target));

  return out.join(' ');
}
