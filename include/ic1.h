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

#include "neug/compiler/function/function.h"
#include "ldbc_common.h"
#include "neug/compiler/function/neug_call_function.h"

namespace neug {
namespace extension {
namespace ldbc_ic {

struct IC1FuncInput : ldbc::LdbcCallInput {
  int64_t person_id;
  std::string first_name;

  void bindParams(const execution::ParamsMap& params) override {
    person_id = params.at("personId").GetValue<int64_t>();
    first_name = params.at("firstName").GetValue<std::string>();
  }
};


struct IC1Function {
  static constexpr const char* name = "ic1";
  static function::function_set getFunctionSet();
};

}  // namespace ldbc_ic
}  // namespace extension
}  // namespace neug
