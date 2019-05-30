#!/usr/bin/env node

const fs = require('fs');
const now = new Date;

const alwaysKeyword = 
    " break case catch class const continue debugger default do else enum export extends" +
    " finally for function if return static switch throw try var while with ";

const alwaysStrictKeyword =
    " implements let package protected interface private public yield ";

// act like unary ops
const unaryOp =
    " delete new typeof void ";

const optionalUnaryOp =
    " await yield ";

const relOp =
    " in instanceof ";

// act like symbols but internal
// => 'import' can also start special statements
// => "undefined" is NOT here, even in strict mode
const neverLabel = 
    " false import null super this true ";

// these act like symbols in most cases, but hash so we can find them
const optionalKeyword =
    " as async await from get of set ";

// keywords that start a decl
const declKeyword =
    " var let const ";

// keywords that start a brace or single statement
const controlKeyword =
    " catch do else if finally for switch try while with ";

// control keywords that consume a ()
const controlParenKeyword =
    " catch if for switch while with ";

// control keywords that must have {}
const controlBraceKeyword =
    " catch finally switch try ";

// reserved until ES3
const oldKeyword = 
    " abstract boolean byte char double final float goto int long native short synchronized" +
    " throws transient volatile ";

// symbols/punctuation that should get hashed
const extraDefines = {
  COMMA: ',',
  SPREAD: '...',
  ARROW: '=>',
  COLON: ':',
  RARRAY: ']',
  DOT: '.',
  STAR: '*',
  INCDEC: '+-',  // nb. this cheats and we use hash of +-
  NOT: '!',
  BITNOT: '~',
  EQUALS: '=',
};

// ways of marking up hashes
const specials = [
  'keyword',
  'strictKeyword',
  'relOp',
  'unaryOp',
  'masquerade',
  'decl',
  'control',
  'controlParen',
  'controlBrace',
  // 'es3Reserved',
];


const maximumSpecialBit = 13;
if (specials.length >= maximumSpecialBit) {
  throw new Error(`too many special bits: ${specials.length}`);
}


function bitValueFor(name) {
  const index = specials.indexOf(name);
  if (index === -1) {
    throw new Error('invalid bit request: ' + name);
  }
  return 1 << index;
}


const pendingNames = new Map();
const queue = (all, ...names) => {
  if (typeof all === 'string') {
    all = all.split(/\s+/).filter(Boolean);
  }

  const bits = names.map((name) => bitValueFor(name));
  const bitsum = bits.reduce((a, b) => a + b, 0);
  for (const cand of all) {
    const prev = pendingNames.get(cand) || 0;
    pendingNames.set(cand, prev | bitsum);
  }
};


const nameToHash = new Map();
const hashToName = new Map();

queue(alwaysKeyword, 'keyword', 'strictKeyword');
queue(alwaysStrictKeyword, 'strictKeyword');
queue(unaryOp, 'keyword', 'unaryOp');
queue(optionalUnaryOp, 'unaryOp');
queue(relOp, 'keyword', 'relOp');
queue(neverLabel, 'masquerade');
queue(optionalKeyword);
queue(declKeyword, 'decl');
queue(controlKeyword, 'control');
queue(controlParenKeyword, 'controlParen');
queue(controlBraceKeyword, 'controlBrace');
// queue(oldKeyword, 'es3Reserved');
const litOnly = Array.from(pendingNames.keys()).sort();

// add other random punctuators _after_ saving litOnly
queue(Object.values(extraDefines));


// now actually generate hashes
pendingNames.forEach((bits, name) => {
  const hash = generateHash(name, bits);
  if (hashToName.has(hash)) {
    throw new Error(`duplicate hash: ${name}=${hash}`);
  }
  hashToName.set(hash, name);
  nameToHash.set(name, hash);
});


function generateHash(s, bits=0) {
  if (bits >= (1 << 13)) {
    throw new Error('using more than 13 bits');
  } else if (!s.length) {
    throw new Error('empty string for hash');
  }

  const ordZero = s.charCodeAt(0) || 0;
  const ordOne = s.charCodeAt(1) || 0;
  if (ordZero >= 128 || ordOne >= 128) {
    throw new Error('ord for char out of range');
  }
  const length = Math.min(s.length, 15);

  let out = bits;        // 0-13
  out += ordZero << 13;  // 13-20
  out += ordOne << 20;   // 20-27
  out += length << 27;   // 27-31
  // we don't use bit 32, avoid being interpreted as signed int

  // can't use << as JS wraps to unsigned here
  if (out >= (2 ** 31)) {
    throw new Error(`out of range of 32-bit int: ${out}, ${out.toString(2)} (${out.toString(2).length})`);
  }

  return out;
}


function renderDefine(defn, value, prefix, js=false) {
  let hadValue = value;
  if (value === null) {
    value = defn;
  }
  const name = (prefix + defn).toUpperCase().padEnd(js ? 0 : 16);
  const hash = nameToHash.get(value);
  if (!hash) {
    throw new Error(`unhashed name: ${value}`);
  }

  if (js) {
    return `export const ${name} = ${hash};\n`
  }

  let out = `#define ${name} ${hash}`;
  if (hadValue) {
    return out.padEnd(36) + `// ${hadValue}\n`;
  }
  return out + '\n';
}


function renderDefines(defines, prefix, js=false) {
  let out;

  if (defines instanceof Array) {
    out = defines.map((value) => renderDefine(value, null, prefix, js));
  } else {
    out = Object.keys(defines).map((defn) => renderDefine(defn, defines[defn], prefix, js));
  }

  out.sort();
  return out.join('');
}


function uniquePrefix(list) {
  list = list.slice();  // clone to sort
  list.sort();
  const out = new Map();
  let prev = -1;
  let options = null;

  for (const opt of list) {
    const cand = opt.charCodeAt(0) || '';
    if (cand !== prev) {
      options = [];
      out.set(cand, options);
      prev = cand;
    }
    options.push(opt.substr(1));
  }
  return out;
}


function renderChain(all, space='') {
  let curr = uniquePrefix(all);
  let chars = [];
  let rest;

  while (curr.size === 1) {
    curr.forEach((internalRest, char) => {
      if (char === '') {
        curr = new Map();
        return;
      }
      chars.push(char);
      curr = uniquePrefix(internalRest);
      rest = internalRest;  // save in case we don't use curr
    });
  }

  return {tail: curr, rest, chars};
}


function renderChoice(all, space='', prefix='') {
  const readNext = `*p++`;

  const {tail, rest, chars} = renderChain(all, space);
  if (chars.length) {
    let out = ``;
    let len = `p - start - 1`;  // safely consumed this many chars (based on conditional progress)

    const conditional = chars.map((char) => {
      return `${readNext} != '${String.fromCharCode(char)}'`;
    });
    const check = String.fromCharCode(...chars);
    prefix += check;
    if (chars.length === 1) {
      len = prefix.length - 1;  // we safely consumed this many chars (one cond, so no sub)
    }

    out += `${space}if (${conditional.join(' || ')}) {
${space}  return ${len};
${space}}\n`;
    out += renderChoice(rest, space, prefix);
    return out;
  }

  if (!tail.size) {
    const d = `lit_${prefix}`.toUpperCase();
    return `${space}_done(${prefix.length}, ${d});\n`;
  }

  let out = `${space}switch (${readNext}) {\n`;
  let hasDefault = false;
  tail.forEach((rest, char) => {
    if (char) {
      const extra = String.fromCharCode(char);
      out += `${space}case '${extra}':\n`;
      out += renderChoice(rest, space + '  ', prefix + extra);
    } else {
      hasDefault = true;
    }
  });
  out += `${space}}\n`;

  if (hasDefault) {
    const d = `lit_${prefix}`.toUpperCase();
    out += `${space}_done(${prefix.length}, ${d});\n`;
  } else {
    out += `${space}return ${prefix.length};  // ${prefix}...\n`;
  }

  return out;
}


function renderSpecial(specials, js=false) {
  const lines = specials.map((name, i) => {
    const upper = name.replace(/[A-Z]/g, (letter) => `_${letter}`).toUpperCase();
    const value = bitValueFor(name);
    if (js) {
      return `export const _${upper} = ${value};\n`;
    }
    return `#define _MASK_${upper.padEnd(16)} ${value}\n`;
  });
  return lines.join('');
}


function main() {
  const helperOutput = `// Generated on ${now}

#include "lit.h"
#include "helper.h"

// ${litOnly.length} candidates:
//   ${litOnly.join(' ')}
int consume_known_lit(char *p, uint32_t *out) {
  char *start = p;
#define _done(len, _out) {*out=_out;return len;}
${renderChoice(litOnly, space='  ')}
#undef _done
}
`;
  fs.writeFileSync('helper.c', helperOutput);


  const helperHeaderOutput = `// Generated on ${now}

#ifndef _HELPER_H
#define _HELPER_H

#include <stdint.h>

int consume_known_lit(char *, uint32_t *);

#endif//_HELPER_H
`;
  fs.writeFileSync('helper.h', helperHeaderOutput);


  const litOutput = `// Generated on ${now}

#ifndef _LIT_H
#define _LIT_H

${renderSpecial(specials)}
${renderDefines(litOnly, 'lit_')}
${renderDefines(extraDefines, 'misc_')}
#endif//_LIT_H
`;
  fs.writeFileSync('lit.h', litOutput);


  const litJSOutput = `// Generated on ${now}

${renderSpecial(specials, true)}
${renderDefines(litOnly, '', true)}
${renderDefines(extraDefines, '$', true)}
`;
  fs.writeFileSync('lit.mjs', litJSOutput);
}

main();
