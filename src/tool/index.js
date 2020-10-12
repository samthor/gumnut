#!/usr/bin/env node

import {processFile} from './split.js';
import build from '../harness/node-harness.js';
import fs from 'fs';
import path from 'path';

const runner = await build();
const sources = process.argv.slice(2).map((source) => {
  return path.resolve(source);
});

const nowKey = (+(new Date().toISOString().replace(/[^\d]/g, ''))).toString(36);

/**
 * Analyzes and rewrites passed files.
 *
 *  - if a file has sideEffects=false, uses as a target
 *  - otherwise, find methods in non-sideEffects or creates new
 */

const state = sources.map((source, index) => {
  const buffer = fs.readFileSync(source);
  const out = processFile(runner, buffer);

  const {sideEffects} = out;
  console.info(source, 'sideEffects:', sideEffects);

  if (index === 0) {
    return out;
  }

  if (sideEffects) {
    throw new TypeError(`additional files cannot have sideEffects=true`);
  }

  return out;
});

const [sourceFile, ...availableFiles] = state;

const availableDeclarations = {};
availableFiles.forEach((file, index) => {
  const rel = importRelative(sources[0], sources[index + 1]);
  const declarations = file.declarations();
  for (const hash in declarations) {
    const name = declarations[hash];
    availableDeclarations[hash] = {name, from: rel};
  }
});

const generatedPartName = `./${path.basename(sources[0])}.part-${nowKey}.js`;
const outputWrites = [];
const imports = {};

const candidateDeclarations = sourceFile.declarations();
for (const hash in candidateDeclarations) {
  const name = candidateDeclarations[hash];
  const extracted = sourceFile.replace(name);
  let from = generatedPartName;
  let externalName = name;

  if (hash in availableDeclarations) {
    // TODO: for now, methods will always be named the same (used in hash)
    const o = availableDeclarations[hash];
    ({from, name: externalName} = o);
    console.warn('got hit', hash, name, '=>', from, externalName);

  } else {
    console.warn('got miss', hash, name);

    // nb. for now we can't rewrite names so everything is its original

    outputWrites.push('export ');
    outputWrites.push(extracted);
  }

  if (!(from in imports)) {
    imports[from] = {};
  }
  imports[from][name] = externalName;
}

console.info('\n//', sources[0]);
console.info('////////////////');

for (const from in imports) {
  const parts = Object.keys(imports[from]).map((name) => {
    const externalName = imports[from][name];
    if (externalName === name) {
      return name;
    }
    return `${externalName} as ${name}`;
  });
  console.info(`import {${parts.join(', ')}} from ${JSON.stringify(from)};`);
}

console.info(sourceFile.render());

console.info('\n//', generatedPartName);
console.info('////////////////');

console.info(outputWrites.join(''));

function importRelative(from, to) {
  const rel = path.relative(path.dirname(from), to);
  if (/^\.{0,2}\//.test(rel)) {
    return rel;
  }
  return `./${rel}`;
}