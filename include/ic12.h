/** Copyright 2020 Alibaba Group Holding Limited.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * 	http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ldbc_common.h"
#include "neug/compiler/function/function.h"
#include "neug/compiler/function/neug_call_function.h"

namespace neug {
namespace extension {
namespace ldbc {

struct IC12FuncInput : ldbc::LdbcCallInput {
  int64_t person_id;
  std::string tag_class_name;

  void bindParams(const execution::ParamsMap& params) override {
    person_id = params.at("personId").GetValue<int64_t>();
    tag_class_name = params.at("tagClassName").GetValue<std::string>();
  }
};

struct IC12Function {
  static constexpr const char* name = "ic12";
  static function::function_set getFunctionSet();
};

}  // namespace ldbc
}  // namespace extension
}  // namespace neug
