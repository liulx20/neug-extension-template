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
#include <string>

#include "ic2.h"
#include "neug/main/neug_db.h"
#include "neug/utils/property/types.h"

extern "C" {
void Init();
}

TEST(IC2Function, FunctionSet) {
  auto function_set = neug::extension::ldbc::IC2Function::getFunctionSet();
  ASSERT_EQ(function_set.size(), 1u);
  EXPECT_EQ(function_set[0]->name, "ic2");
  auto* call_func =
      dynamic_cast<neug::function::NeugCallFunction*>(function_set[0].get());
  ASSERT_NE(call_func, nullptr);
  EXPECT_EQ(call_func->outputColumns.size(), 6u);
}

#ifdef LDBC_EXTENSION_LIB
namespace {

void setup_ldbc_mini_graph(neug::Connection* conn) {
  auto run = [&](const std::string& q) {
    auto res = conn->Query(q);
    ASSERT_TRUE(res.has_value()) << res.error().ToString() << " query: " << q;
  };
  run("CREATE NODE TABLE PERSON(id INT64, firstName STRING, lastName STRING, "
      "PRIMARY KEY(id));");
  run("CREATE NODE TABLE POST(id INT64, imageFile STRING, content STRING, "
      "length INT32, creationDate TIMESTAMP, PRIMARY KEY(id));");
  run("CREATE NODE TABLE COMMENT(id INT64, content STRING, creationDate "
      "TIMESTAMP, PRIMARY KEY(id));");
  run("CREATE REL TABLE KNOWS(FROM PERSON TO PERSON);");
  run("CREATE REL TABLE HASCREATOR(FROM POST TO PERSON, FROM COMMENT TO "
      "PERSON, creationDate TIMESTAMP);");
  run("CREATE (root:PERSON {id: 1, firstName: 'A', lastName: 'Root'}), "
      "(friend:PERSON {id: 2, firstName: 'B', lastName: 'Friend'});");
  run("MATCH (a:PERSON {id:1}), (b:PERSON {id:2}) CREATE (a)-[:KNOWS]->(b);");
  run("CREATE (post:POST {id: 100, imageFile: '', content: 'hello', length: "
      "5, creationDate: timestamp('2012-04-09 18:45:05.842')});");
  run("MATCH (p:POST {id:100}), (f:PERSON {id:2}) CREATE "
      "(p)-[:HASCREATOR {creationDate: timestamp('2012-04-09 "
      "18:45:05.842')}]->(f);");
}

}  // namespace

TEST(IC2Function, CallWithLiteralMillis) {
  const std::string ext_path = LDBC_EXTENSION_LIB;
  ASSERT_TRUE(std::filesystem::exists(ext_path)) << ext_path;

  const std::string db_path = "/tmp/ic2_call_literal_test";
  std::filesystem::remove_all(db_path);

  neug::NeugDB db;
  ASSERT_TRUE(db.Open(db_path));
  auto conn = db.Connect();
  ASSERT_NE(conn, nullptr);
  setup_ldbc_mini_graph(conn.get());

  auto load_res = conn->Query("LOAD '" + ext_path + "'");
  ASSERT_TRUE(load_res.has_value()) << load_res.error().ToString();

  const int64_t max_date_ms =
      neug::DateTime("2012-04-11 00:00:00.000").milli_second;
  auto ic2_res = conn->Query(
      "CALL ic2(1, " + std::to_string(max_date_ms) +
          ") "
          "YIELD personId, personFirstName, personLastName, messageId, "
          "messageContent, messageCreationDate "
          "RETURN personId, personFirstName, personLastName, messageId, "
          "messageContent, messageCreationDate",
      "read");
  ASSERT_TRUE(ic2_res.has_value()) << ic2_res.error().ToString();
  ASSERT_EQ(ic2_res->length(), 1u);
  ic2_res->Reset();
  ASSERT_TRUE(ic2_res->hasNext());
  EXPECT_EQ(ic2_res->GetInt64(0), 2);
  EXPECT_EQ(ic2_res->GetString(1), "B");
  EXPECT_EQ(ic2_res->GetInt64(3), 100);
  EXPECT_EQ(ic2_res->GetString(4), "hello");

  conn.reset();
  db.Close();
  std::filesystem::remove_all(db_path);
}
#endif
