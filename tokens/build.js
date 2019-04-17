
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


const readNext = `*(++p)`;


const known = new Map();


function generateHash(s) {
  let out = 0;
  const size = Math.min(3, s.length);
  for (let i = 0; i < size; ++i) {
    out += (s.charCodeAt(i) << (i * 8));
  }
  out += Math.min(s.length, 255) << 24;
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


function group(list) {
  // group by first letter, assume sorted
  if (!list.length) {
    return [];
  }
  list = list.slice();  // clone to sort
  list.sort();
  const result = [];

  let focus = 0;
  let low = list[focus].charCodeAt(0);  // first letter of ...
  let high = low;

  while (++focus < list.length) {
    const cand = list[focus].charCodeAt(0);
    if (cand === high || cand === high + 1) {
      high = cand;
      continue;  // great, same or just up one
    }

    // otherwise, allocate new group
    result.push(low, high);
    low = cand;
    high = cand;
  }

  result.push(low, high);
  return result;
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
    let len = `p - start`;  // safely consumed this many chars (based on conditional progress)

    const conditional = chars.map((char) => {
      return `${readNext} == ${char}`;
    });
    prefix += String.fromCharCode(...chars);
    if (chars.length === 1) {
      len = prefix.length - 1;  // we safely consumed this many chars (one cond, so no sub)
    }

    out += `${space}if (!(${conditional.join(' && ')})) {
${space}  return ${len};
${space}}\n`;
    out += renderChoice(rest, space, prefix);
    return out;
  }

  if (!tail.size) {
    return `${space}_done(${prefix.length}, ${generateHash(prefix)});  // ${prefix}\n`;
  }

  let out = `${space}switch (${readNext}) {\n`;
  tail.forEach((rest, char) => {
    if (char) {
      out += `${space}case ${char}:\n`;
      out += renderChoice(rest, space + '  ', prefix + String.fromCharCode(char));
    } else {
      out += `${space}default:
${space}  _done(${prefix.length}, ${generateHash(prefix)});  // ${prefix}
`;
    }
  });
  out += `${space}}\n`;
  return out;
}



process(alwaysKeyword);
process(alwaysStrictKeyword);
process(neverLabel);
process(optionalKeyword);
//process(oldKeyword);

const all = filterTo('');
console.info(`// Generated on ${new Date}`);
console.info(renderChoice(all));
