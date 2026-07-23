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

#include "ic3.h"
#include "neug/main/neug_db.h"
#include "neug/utils/property/types.h"

extern "C" {
void Init();
}

TEST(IC3Function, FunctionSet) {
  auto function_set = neug::extension::ldbc::IC3Function::getFunctionSet();
  ASSERT_EQ(function_set.size(), 1u);
  EXPECT_EQ(function_set[0]->name, "ic3");
  auto* call_func =
      dynamic_cast<neug::function::NeugCallFunction*>(function_set[0].get());
  ASSERT_NE(call_func, nullptr);
  EXPECT_EQ(call_func->outputColumns.size(), 6u);
}

#ifdef LDBC_EXTENSION_LIB
namespace {

void setup_ic3_mini_graph(neug::Connection* conn) {
  auto run = [&](const std::string& q) {
    auto res = conn->Query(q);
    ASSERT_TRUE(res.has_value()) << res.error().ToString() << " query: " << q;
  };
  run("CREATE NODE TABLE PERSON(id INT64, firstName STRING, lastName STRING, "
      "PRIMARY KEY(id));");
  run("CREATE NODE TABLE POST(id INT64, content STRING, creationDate "
      "TIMESTAMP, "
      "PRIMARY KEY(id));");
  run("CREATE NODE TABLE COMMENT(id INT64, content STRING, creationDate "
      "TIMESTAMP, PRIMARY KEY(id));");
  run("CREATE NODE TABLE PLACE(id INT64, name STRING, PRIMARY KEY(id));");
  run("CREATE REL TABLE KNOWS(FROM PERSON TO PERSON);");
  run("CREATE REL TABLE HASCREATOR(FROM POST TO PERSON, FROM COMMENT TO "
      "PERSON);");
  run("CREATE REL TABLE ISLOCATEDIN(FROM PERSON TO PLACE, FROM POST TO PLACE, "
      "FROM COMMENT TO PLACE);");
  run("CREATE REL TABLE ISPARTOF(FROM PLACE TO PLACE);");
  run("CREATE (root:PERSON {id: 1, firstName: 'A', lastName: 'Root'}), "
      "(friend:PERSON {id: 2, firstName: 'B', lastName: 'Friend'}), "
      "(cx:PLACE {id: 10, name: 'Puerto_Rico'}), "
      "(cy:PLACE {id: 11, name: 'Republic_of_Macedonia'}), "
      "(city:PLACE {id: 12, name: 'NeutralCity'});");
  run("MATCH (a:PERSON {id:1}), (b:PERSON {id:2}) CREATE (a)-[:KNOWS]->(b);");
  run("MATCH (f:PERSON {id:2}), (c:PLACE {id:12}) CREATE "
      "(f)-[:ISLOCATEDIN]->(c);");
  run("CREATE (post_x:POST {id: 100, content: 'x', creationDate: "
      "timestamp('2010-12-10 00:00:00.000')}), "
      "(post_y:POST {id: 101, content: 'y', creationDate: "
      "timestamp('2010-12-15 00:00:00.000')});");
  run("MATCH (p:POST {id:100}), (cx:PLACE {id:10}), (f:PERSON {id:2}) "
      "CREATE (p)-[:ISLOCATEDIN]->(cx), (p)-[:HASCREATOR]->(f);");
  run("MATCH (p:POST {id:101}), (cy:PLACE {id:11}), (f:PERSON {id:2}) "
      "CREATE (p)-[:ISLOCATEDIN]->(cy), (p)-[:HASCREATOR]->(f);");
}

}  // namespace

TEST(IC3Function, CallWithLiteralArgs) {
  const std::string ext_path = LDBC_EXTENSION_LIB;
  ASSERT_TRUE(std::filesystem::exists(ext_path)) << ext_path;

  const std::string db_path = "/tmp/ic3_call_literal_test";
  std::filesystem::remove_all(db_path);

  neug::NeugDB db;
  ASSERT_TRUE(db.Open(db_path));
  auto conn = db.Connect();
  ASSERT_NE(conn, nullptr);
  setup_ic3_mini_graph(conn.get());

  auto load_res = conn->Query("LOAD '" + ext_path + "'");
  ASSERT_TRUE(load_res.has_value()) << load_res.error().ToString();

  const int64_t start_date_ms =
      neug::DateTime("2010-12-01 00:00:00.000").milli_second;
  auto ic3_res = conn->Query(
      "CALL ic3(1, 'Puerto_Rico', 'Republic_of_Macedonia', " +
          std::to_string(start_date_ms) +
          ", 30) "
          "YIELD personId, personFirstName, personLastName, countryXCount, "
          "countryYCount, totalCount "
          "RETURN personId, personFirstName, personLastName, countryXCount, "
          "countryYCount, totalCount",
      "read");
  ASSERT_TRUE(ic3_res.has_value()) << ic3_res.error().ToString();
  ASSERT_EQ(ic3_res->length(), 1u);
  ic3_res->Reset();
  ASSERT_TRUE(ic3_res->hasNext());
  EXPECT_EQ(ic3_res->GetInt64(0), 2);
  EXPECT_EQ(ic3_res->GetString(1), "B");
  EXPECT_EQ(ic3_res->GetInt64(3), 1);
  EXPECT_EQ(ic3_res->GetInt64(4), 1);
  EXPECT_EQ(ic3_res->GetInt64(5), 2);

  conn.reset();
  db.Close();
  std::filesystem::remove_all(db_path);
}
#endif
