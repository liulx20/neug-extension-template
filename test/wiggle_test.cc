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

#include <gtest/gtest.h>
#include <vector>

#include "neug/common/types/value.h"
#include "wiggle_function.h"

TEST(WiggleFunction, Exec) {
  std::vector<neug::Value> args;
  args.push_back(neug::Value::STRING("Sam"));
  auto result = neug::extension::wiggle::WiggleFunction::Exec(args);
  EXPECT_EQ(neug::StringValue::Get(result), "~~~~~~~~~🌐 Sam");
}

TEST(WiggleFunction, FunctionSet) {
  auto functionSet = neug::extension::wiggle::WiggleFunction::getFunctionSet();
  ASSERT_EQ(functionSet.size(), 1u);
  EXPECT_EQ(functionSet[0]->name, "wiggle");
}
