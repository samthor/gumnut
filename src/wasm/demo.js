#!/usr/bin/env node
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

import build from './rewriter.js';

const resolve = (importee, importer) => {
  // importee is from import/export, importer is the current file
  // TODO: this is a terrible resolver, don't do this
  return `/node_modules/${importee}/index.js`;
};

build(resolve).then((rewriter) => {
  const s = rewriter(process.argv[2] || 'demo.js');
  s.pipe(process.stdout);
});
