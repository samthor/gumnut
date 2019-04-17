#!/usr/bin/env node

const fs = require('fs');
const now = new Date;

const alwaysKeyword = 
    " async break case catch class const continue debugger default delete do else enum export" +
    " extends finally for function if import new return static switch throw try typeof var void" +
    " while with ";

const alwaysStrictKeyword =
    " implements package protected interface private public ";

const neverLabel = 
    " false in instanceof null super this true ";

const optionalKeyword =
    " as await from let yield ";

// reserved until ES3
const oldKeyword = 
    " abstract boolean byte char double final float goto int long native short synchronized" +
    " throws transient volatile ";

const extraDefines = {
  COMMA: ',',
  SPREAD: '...',
  ARROW: '=>',
  COLON: ':',
  DOT: '.',
  STAR: '*',
  DOUBLE_ADD: '++',
  DOUBLE_SUB: '--',
};


const readNext = `*p++`;


const known = new Map();


function renderDefine(defn, value=null, prefix='lit_') {
  let hadValue = value;
  if (value === null) {
    value = defn;
  }
  const name = (prefix + defn).toUpperCase().padEnd(16);
  const hash = generateHash(value);
  let out = `#define ${name} ${hash}`;

  if (hadValue) {
    return out.padEnd(36) + `// ${hadValue}\n`;
  }
  return out + '\n';
}


function renderDefines() {
  const all = Array.from(known.values()).map((value) => renderDefine(value));
  all.sort();
  return all.join('');
}


function renderExtraDefines(map, prefix='_lit_') {
  const all = [];
  for (const key in map) {
    const value = map[key];
    all.push(renderDefine(key, value, prefix));
  }
  return all.join('');
}


function generateHash(s) {
  let out = 0;
  const size = Math.min(2, s.length);
  for (let i = 0; i < size; ++i) {
    out += (s.charCodeAt(i) << (i * 8));
  }
  out += Math.min(s.length, 255) << 16;
  return out;
}


function process(all) {
  all = all.split(/\s+/).filter(Boolean);

  for (const keyword of all) {
    const hash = generateHash(keyword);
    if (known.has(hash)) {
      throw new Error(`duplicate hash: ${keyword}=${hash}`);
    }

    known.set(hash, keyword);
  }
}


function filterTo(prefix) {
  const out = [];
  known.forEach((keyword) => {
    if (keyword.startsWith(prefix)) {
      out.push(keyword.substr(prefix.length));
    }
  });
  out.sort();
  return out;
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
  const {tail, rest, chars} = renderChain(all, space);
  if (chars.length) {
    let out = ``;
    let len = `p - start - 1`;  // safely consumed this many chars (based on conditional progress)

    const conditional = chars.map((char) => {
      return `${readNext} != ${char}`;
    });
    const check = String.fromCharCode(...chars)
    prefix += check;
    if (chars.length === 1) {
      len = prefix.length - 1;  // we safely consumed this many chars (one cond, so no sub)
    }

    out += `${space}if (${conditional.join(' || ')}) {
${space}  // != "${check}"
${space}  return ${len};
${space}}\n`;
    out += renderChoice(rest, space, prefix);
    return out;
  }

  if (!tail.size) {
    return `${space}_done(${prefix.length}, ${generateHash(prefix)});  // ${prefix}\n`;
  }

  let out = `${space}switch (${readNext}) {\n`;
  let hasDefault = false;
  tail.forEach((rest, char) => {
    if (char) {
      const extra = String.fromCharCode(char);
      out += `${space}case ${char}:  // '${extra}'\n`;
      out += renderChoice(rest, space + '  ', prefix + extra);
    } else {
      hasDefault = true;
    }
  });
  out += `${space}}\n`;

  if (hasDefault) {
    out += `${space}_done(${prefix.length}, ${generateHash(prefix)});  // ${prefix}\n`;
  } else {
    out += `${space}return ${prefix.length};  // ${prefix}...\n`
  }

  return out;
}



process(alwaysKeyword);
process(alwaysStrictKeyword);
process(neverLabel);
process(optionalKeyword);
//process(oldKeyword);


const all = Array.from(known.values()).sort();
const helperOutput = `// Generated on ${now}
// ${all.length} candidates:
//   ${all.join(' ')}
int consume_known_lit(char *p, uint32_t *out) {
  char *start = p;
#define _done(len, _out) {*out=_out;return len;}
${renderChoice(filterTo(''), space='  ')}
#undef _done
}
`;
fs.writeFileSync('helper.c', helperOutput);


const defineOutput = `// Generated on ${now}
${renderDefines()}
${renderExtraDefines(extraDefines)}
`;
fs.writeFileSync('helper.h', defineOutput);

