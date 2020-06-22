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

import wrapper from '../wasm/node.js';
import path from 'path';
import {specials, types} from '../wasm/wrap.js';
import assert from 'assert';
import mocha from 'mocha';

const {suite, test, before} = mocha;
const __dirname = path.dirname(import.meta.url.split(':')[1]);

let runner;

before(async () => {
  runner = await wrapper();
});

suite('test', () => {
  test('simple', () => {
    const expected = [
      types.comment, 0,
      // import ...
      types.keyword, 0,
      types.symbol, specials.declare | specials.top,
      types.keyword, 0,
      types.string, specials.modulePath,
      types.semicolon, 0,
      // async function ...
      types.keyword, 0,
      types.keyword, 0,
      types.symbol, specials.declare | specials.top,
      types.paren, 0,
      types.symbol, specials.declare | specials.top,
      types.close, 0,
      types.brace, 0,
      types.symbol, 0,
      types.op, 0,
      types.lit, specials.property,
      types.paren, 0,
      types.string, 0,
      types.close, 0,
      types.semicolon, 0,
      types.close, 0,
      // var a = ...
      types.keyword, 0,
      types.symbol, specials.declare | specials.top,
      types.op, 0,
      types.keyword, 0,
      types.paren, 0,
      types.close, 0,
      types.op, 0,
      types.number, 0,
      types.semicolon, 0,
      // let b = ...
      types.keyword, 0,
      types.symbol, specials.declare,
      types.op, 0,
      types.symbol, 0,
      types.paren, 0,
      types.close, 0,
      types.semicolon, 0,
    ];

    const {run, token} = runner(path.join(__dirname, 'data/simple.js'));
    let index = 0;
    run((special) => {
      const expectedType = expected[index+0];
      const expectedSpecial = expected[index+1];

      assert.equal(special, expectedSpecial, `token '${token.string()}' did not match special`);
      assert.equal(token.type(), expectedType, `token '${token.string()}' did not match type`);

      index += 2;
    });

    assert.equal(index, expected.length, `invalid token count`);
  });

  test('rewriter', () => {
    const {run, token} = runner(path.join(__dirname, 'data/simple.js'));

    // Remove comments, and replace module paths with made-up ones.
    const s = run((special) => {
      if (token.type() === types.comment) {
        return '';
      }

      if (special & specials.modulePath) {
        return '\'made_up_module\'';
      }
    });

    let out = '';
    for (;;) {
      const next = s.read();
      if (next === null) {
        break;
      }
      out += next.toString('utf-8');
    }

    assert.equal(out, `

import foo from 'made_up_module';

async function foo(q) {
  console.info('hello');
}

var a = async () => 123;
let b = async();
`);
  });
});
