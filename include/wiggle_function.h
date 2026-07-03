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

#include <algorithm>
#include <string>
#include <vector>

#include "neug/compiler/function/function.h"
#include "neug/compiler/function/neug_scalar_function.h"
#include "neug/execution/common/types/value.h"
#include "neug/utils/exception/exception.h"

namespace neug {
namespace extension {
namespace wiggle {

struct WiggleFunction {
  static constexpr const char* name = "wiggle";

  static function::function_set getFunctionSet() {
    function::function_set functionSet;
    functionSet.emplace_back(std::make_unique<function::NeugScalarFunction>(
        name, std::vector<common::DataTypeId>{common::DataTypeId::kVarchar},
        common::DataTypeId::kVarchar, WiggleFunction::Exec));
    return functionSet;
  }

  static execution::Value Exec(const std::vector<execution::Value>& args) {
    if (args.size() != 1) {
      THROW_RUNTIME_ERROR("WIGGLE: expect exactly 1 argument, got " +
                          std::to_string(args.size()));
    }
    const auto& val = args[0];
    if (val.type().id() != common::DataTypeId::kVarchar) {
      THROW_RUNTIME_ERROR("WIGGLE: input value is not a string");
    }
    std::string str(execution::StringValue::Get(val));
    return execution::Value::STRING("~~~~~~~~~🌐 " + str);
  }
};

}  // namespace wiggle
}  // namespace extension
}  // namespace neug
