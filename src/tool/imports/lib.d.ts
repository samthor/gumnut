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

/// <reference types="node" />

import {Readable} from 'stream';

/**
 * Builds a method which rewrites imports from a passed filename into ESM found inside node_modules.
 *
 * This emits relative paths to node_modules, rather than absolute ones.
 */
export default function buildModuleImportRewriter(resolver?: typeof defaultResolver): Promise<(file: string) => Readable>;

/**
 * The default resolver used to resolve ESM.
 */
export const defaultResolver: (importee: string, importer: string) => string|void;

/**
 * Internal node type for package.json' imports/exports.
 */
export type InternalPackageModuleNode = {[name: string]: InternalPackageModuleNode} | string;

export type InternalPackageJson = {
  [name: string]: number | string | InternalPackageJson,
  exports?: InternalPackageModuleNode,
  imports?: InternalPackageModuleNode,
};

export type InternalPackageJsonNode = {
  [name: string]: number | string | InternalPackageJsonNode,
};
