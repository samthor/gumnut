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

import buildHarness from '../harness/node-harness.js';
import buildRewriter from '../harness/node-rewriter.js';
import {specials, types} from '../harness/common.js';
import * as lit from '../tokens/lit.js';

import test from 'ava';

const harness = await buildHarness();
const {run, token} = buildRewriter(harness);

test.serial('simple', (t) => {
  const expected = [
    // import ...
    types.keyword, lit.IMPORT,
    types.symbol, specials.declare | specials.top,
    types.keyword, lit.FROM,
    types.string, specials.external,
    types.semicolon, 0,
    // async function ...
    types.keyword, lit.ASYNC,
    types.keyword, lit.FUNCTION,
    types.symbol, specials.declare | specials.change,
    types.paren, 0,
    types.symbol, specials.declare | specials.top,
    types.close, types.paren,
    types.block, 0,
    types.symbol, 0,
    types.op, lit.$DOT,
    types.lit, specials.property,
    types.paren, 0,
    types.string, 0,
    types.close, types.paren,
    types.semicolon, 0,
    types.close, types.block,
    // var a = ...
    types.keyword, lit.VAR,
    types.symbol, specials.declare | specials.top | specials.change,
    types.op, lit.$EQUALS,
    types.keyword, lit.ASYNC,
    types.paren, 0,
    types.close, types.paren,
    types.op, lit.$ARROW,
    types.number, 0,
    types.semicolon, 0,
    // let b = ...
    types.keyword, lit.LET,
    types.symbol, specials.declare | specials.change,
    types.op, lit.$EQUALS,
    types.symbol, 0,
    types.paren, 0,
    types.close, types.paren,
    types.semicolon, 0,
  ];

  let index = 0;

  const callback = () => {
    const expectedType = expected[index+0];
    const expectedSpecial = expected[index+1];

    const pos = index >> 1;

    t.is(token.special(), expectedSpecial, `token ${pos} '${token.string()}' did not match special`);
    t.is(token.type(), expectedType, `token ${pos} '${token.string()}' did not match type`);

    index += 2;
  };

  const {pathname} = new URL('data/simple.js', import.meta.url);
  run(pathname, {callback});

  t.is(index, expected.length, `invalid token count`);
});

test.serial('rewriter', (t) => {
  const callback = () => {
    if (token.special() === specials.external && token.type() === types.string) {
      return '\'made_up_module\'';
    }
  };

  const {pathname} = new URL('data/simple.js', import.meta.url);
  const s = run(pathname, {callback});

  let out = '';
  for (;;) {
    const next = s.read();
    if (next === null) {
      break;
    }
    out += next.toString('utf-8');
  }

  t.is(out, `/*
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

import foo from 'made_up_module';

async function foo(q) {
  console.info('hello');
}

var a = async () => 123;
let b = async();
`);
});
