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
#include <vector>

#include "neug/compiler/function/function.h"
#include "ldbc_common.h"
#include "neug/compiler/function/neug_call_function.h"
#include "neug/execution/common/context.h"
#include "neug/storages/graph/graph_interface.h"

namespace neug {
namespace extension {
namespace ldbc_ic {

struct IC2FuncInput : ldbc::LdbcCallInput {};

struct IC2Function {
  static constexpr const char* name = "ic2";

  static function::function_set getFunctionSet();
};

}  // namespace ldbc_ic
}  // namespace extension
}  // namespace neug
