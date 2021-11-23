'use strict';

var num = 1;
var re = / /* num */ 123/g;
console.info(re);

var g = 1;

/**
 * APPROACH: const/var/let start "decl", finish on ; or invalid (newline) math
 * parser :(
 */

const z = 100
,  // this works on const above
asdfads = z
  /123/g;
// this sucks because we revert after every "," in var/const/let

var q
/123/g;

var q = 'bar'
/123/g;
console.info('will ba NaN beecause /123/g as division', q);

asdfads = 100;

/**
 * APPROACH: easy
 */

if (1) /123/g;

if (1) {} /123/g;

void {} /123/g;
{} /123/g;

/**
 * APPROACH: void starts normal stack
 */

function foo() {} /123/g;
void function foo() {} /123/g;

class Other {};

class x extends Other {} /123/g;
void class x extends Other {} /123/g;

/**
 * APPROACH: not sure, probably need parser (same as for ++/--)
 */

var of = 123;
of /123/g;
for (of of /123/g) {  // same for "in", but is always keyword
}

let someVal = {};
for (((((someVal).foo))) of [1,2,3]) {
  console.info({someVal});
}

/**
 * APPROACH: easy, "from" can ingest string
 */

import { x as y } from './foo.js'
/123/g;
