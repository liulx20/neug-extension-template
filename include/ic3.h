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
#include <memory>
#include <string>
#include <vector>

#include "ldbc_common.h"
#include "neug/compiler/function/function.h"
#include "neug/compiler/function/neug_call_function.h"
#include "neug/execution/common/context.h"
#include "neug/storages/graph/graph_interface.h"

namespace neug {
namespace extension {
namespace ldbc {

struct IC3FuncInput : ldbc::LdbcCallInput {
  int64_t person_id;
  std::string country_x_name;
  std::string country_y_name;
  int64_t start_date_ms;
  int64_t duration_days;

  std::unique_ptr<function::CallFuncInputBase> bindParams(
      const execution::ParamsMap& params) const override {
    return bind_call_params(
        *this, params, [](IC3FuncInput& in, const execution::ParamsMap& p) {
          in.person_id = p.at("personId").GetValue<int64_t>();
          in.country_x_name = p.at("countryXName").GetValue<std::string>();
          in.country_y_name = p.at("countryYName").GetValue<std::string>();
          in.start_date_ms = p.at("startDate").GetValue<int64_t>();
          in.duration_days = p.at("durationDays").GetValue<int64_t>();
        });
  }
};

struct IC3Function {
  static constexpr const char* name = "ic3";
  static function::function_set getFunctionSet();
};

}  // namespace ldbc
}  // namespace extension
}  // namespace neug
