/*
 * Copyright 2017 Sam Thorogood. All rights reserved.
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

int isnum(char c);
int is_keyword(char *s, int len);
int is_reserved_word(char *s, int len);
int is_control_keyword(char *s, int len);
int is_restrict_keyword(char *s, int len);
int is_hoist_keyword(char *s, int len);
int is_expr_keyword(char *s, int len);
int is_labellike_keyword(char *s, int len);
int is_begin_expr_keyword(char *s, int len);
int is_op_keyword(char *s, int len);
int is_decl_keyword(char *s, int len);
int is_label_keyword(char *s, int len);
int is_isolated_keyword(char *s, int len);
int is_double_addsub(char *s, int len);
int is_getset(char *s, int len);