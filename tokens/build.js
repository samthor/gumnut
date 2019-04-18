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
  DOT: '.',
  STAR: '*',
  INCDEC: '+-',  // nb. this cheats and we use hash of +-
};

// ways of marking up hashes
const special = {
  KEYWORD: 1,
  STRICT_KEYWORD: 2,
  REL_OP: 4,
  UNARY_OP: 8,
  MASQUERADE: 16,
  DECL: 32,
  CONTROL: 64,
  CONTROL_PAREN: 128,
};


const pendingNames = new Map();
const queue = (all, bits=0) => {
  if (typeof all === 'string') {
    all = all.split(/\s+/).filter(Boolean);
  }
  for (const cand of all) {
    const prev = pendingNames.get(cand) || 0;
    pendingNames.set(cand, prev | bits)
  }
};


const nameToHash = new Map();
const hashToName = new Map();

queue(alwaysKeyword, special.KEYWORD | special.STRICT_KEYWORD);
queue(alwaysStrictKeyword, special.STRICT_KEYWORD);
queue(unaryOp, special.KEYWORD | special.UNARY_OP);
queue(optionalUnaryOp, special.UNARY_OP);
queue(relOp, special.KEYWORD | special.REL_OP);
queue(neverLabel, special.MASQUERADE);
queue(optionalKeyword);
queue(declKeyword, special.DECL);
queue(controlKeyword, special.CONTROL);
queue(controlParenKeyword, special.CONTROL_PAREN);
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
  // 0: bits
  // 1: s[0]
  // 2: s[1]
  // 3: length
  let out = Math.min(bits, 255);

  if (s.length > 0) {
    out += s.charCodeAt(0) << 8;
    if (s.length > 1) {
      out += s.charCodeAt(1) << 16;
    }
  }

  out += Math.min(s.length, 255) << 24;
  return out;
}


function renderDefine(defn, value, prefix) {
  let hadValue = value;
  if (value === null) {
    value = defn;
  }
  const name = (prefix + defn).toUpperCase().padEnd(16);
  const hash = nameToHash.get(value);
  if (!hash) {
    throw new Error(`unhashed name: ${value}`);
  }
  let out = `#define ${name} ${hash}`;

  if (hadValue) {
    return out.padEnd(36) + `// ${hadValue}\n`;
  }
  return out + '\n';
}


function renderDefines(defines, prefix) {
  let out;

  if (defines instanceof Array) {
    out = defines.map((value) => renderDefine(value, null, prefix));
  } else {
    out = Object.keys(defines).map((defn) => renderDefine(defn, defines[defn], prefix));
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
    const check = String.fromCharCode(...chars)
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
    out += `${space}return ${prefix.length};  // ${prefix}...\n`
  }

  return out;
}


function renderSpecial(special) {
  const all = [];
  for (const key in special) {
    all.push([special[key], key]);
  }
  all.sort((a, b) => a[0] - b[0]);

  const lines = all.map(([value, key]) => {
    return `#define _MASK_${key.padEnd(16)} ${value}\n`
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

${renderSpecial(special)}
${renderDefines(litOnly, 'lit_')}
${renderDefines(extraDefines, 'misc_')}
#endif//_LIT_H
`;
  fs.writeFileSync('lit.h', litOutput);
}

main();
