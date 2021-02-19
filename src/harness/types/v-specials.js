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

export const sameline = 1;
export const declare = 2;
export const top = 4;
export const property = 8;
export const change = 16;
export const external = 32;
export const destructuring = 64;
const _default = 128;
export {_default as default};
export const lit = 1073741824;  // this is (1 << 30) but tsc doesn't like expanding it