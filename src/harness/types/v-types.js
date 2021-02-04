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

export const eof = 0;
export const lit = 1;
export const semicolon = 2;
export const op = 3;
export const colon = 4;
export const brace = 5;
export const array = 6;
export const paren = 7;
export const ternary = 8;
export const close = 9;
export const string = 10;
export const regexp = 11;
export const number = 12;
export const symbol = 13;
export const keyword = 14;
export const label = 15;
export const block = 16;