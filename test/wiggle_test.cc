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
#include <filesystem>
#include <vector>

#include "neug/common/types/value.h"
#include "neug/main/neug_db.h"
#include "wiggle_function.h"

extern "C" {
void Init();
const char* Name();
}

TEST(WiggleExtension, Name) { EXPECT_STREQ(Name(), "WIGGLE"); }

TEST(WiggleExtension, Exec) {
  std::vector<neug::Value> args;
  args.push_back(neug::Value::STRING("Sam"));
  auto result = neug::extension::wiggle::WiggleFunction::Exec(args);
  EXPECT_EQ(neug::StringValue::Get(result), "~~~~~~~~~🌐 Sam");
}

TEST(WiggleExtension, FunctionSet) {
  auto functionSet = neug::extension::wiggle::WiggleFunction::getFunctionSet();
  ASSERT_EQ(functionSet.size(), 1u);
  EXPECT_EQ(functionSet[0]->name, "wiggle");
}

#ifdef WIGGLE_EXTENSION_LIB
TEST(WiggleExtension, LoadByAbsolutePath) {
  const std::string ext_path = WIGGLE_EXTENSION_LIB;
  ASSERT_TRUE(std::filesystem::exists(ext_path)) << ext_path;

  const std::string db_path = "/tmp/wiggle_abs_load_test";
  std::filesystem::remove_all(db_path);

  neug::NeugDB db;
  ASSERT_TRUE(db.Open(db_path));
  auto conn = db.Connect();
  ASSERT_NE(conn, nullptr);

  auto load_res = conn->Query("LOAD '" + ext_path + "'");
  ASSERT_TRUE(load_res.has_value()) << load_res.error().ToString();

  conn.reset();
  db.Close();
  std::filesystem::remove_all(db_path);
}
#endif
