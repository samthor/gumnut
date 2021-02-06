/*
 * Copyright 2021 Sam Thorogood.
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
 
import test from 'ava';
import * as fs from 'fs';

import {defaultBrowserResolver as r, resolver} from '../resolver.js';

const {pathname: importer} = new URL('./testdata/index.js', import.meta.url);

const {pathname: packagePath} = new URL('./testdata/package.json', import.meta.url);
/** @type {string} */
const fakePackageName = JSON.parse(fs.readFileSync(packagePath, 'utf-8')).name;

test('resolves legacy import', t => {
  t.is('./node_modules/fake-package/esm.mjs', r('fake-package', importer), 'default import is returned');
  t.is('./node_modules/fake-package/index.js', r('fake-package/index.js', importer), 'real path is allowed');
  t.is(undefined, r('fake-package/index-doesnotexist.js', importer), 'missing path is skipped');
});

test('resolves constraint exports', t => {
  t.is('./node_modules/exports-package/node.js#browser', r('exports-package', importer), 'browser import is returned, not node');
  t.is('./node_modules/exports-package/node.js#node', resolver(['node'], 'exports-package', importer), 'specific constraints exports node');
});

test('resolves star exports', t => {
  t.is('./node_modules/exports-package/bar/other.js', r('exports-package/foo/other.js', importer));
  t.is(undefined, r('exports-package/foo', importer), 'star imports do not export index.js');

  // nb. Technically these following aren't resolved by Node either, but we'll allow it.
  t.is('./node_modules/exports-package/bar/other.js', r('exports-package/foo/other', importer));
  t.is('./node_modules/exports-package/bar/index.js', r('exports-package/foo/index', importer));
});

test('falls back to open', t => {
  t.is('./node_modules/exports-package/bar/other.js', r('exports-package/bar/other', importer), 'allows unexported file anyway');
});

test('hides .d.ts only', t => {
  const out1 = r('fake-package/solo-types.js', importer);
  t.assert(out1?.startsWith('data:text/javascript;'), 'should hide with empty base64');

  const out2 = r('./node_modules/fake-package/solo-types.js', importer);
  t.assert(out2?.startsWith('data:text/javascript;'), 'hides even while not resolving');

  const out3 = r('fake-package/peer-types.js', importer);
  t.is(out3, './node_modules/fake-package/peer-types.js', 'don\'t hide with peer file');
});

test('resolves self-package', t => {
  t.is(r(fakePackageName, importer), './blah/file.js');
  t.is(r(`${fakePackageName}/package.json`, importer), './package.json');
});
