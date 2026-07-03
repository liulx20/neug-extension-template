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

#include "is1.h"
#include "is2.h"
#include "is3.h"
#include "is4.h"
#include "is5.h"
#include "is6.h"
#include "is7.h"
#include "neug/main/neug_db.h"
#include "neug/utils/property/types.h"

TEST(ISFunctions, FunctionSets) {
  EXPECT_EQ(neug::extension::ldbc_ic::IS1Function::getFunctionSet().size(), 1u);
  EXPECT_EQ(neug::extension::ldbc_ic::IS2Function::getFunctionSet().size(), 1u);
  EXPECT_EQ(neug::extension::ldbc_ic::IS3Function::getFunctionSet().size(), 1u);
  EXPECT_EQ(neug::extension::ldbc_ic::IS4Function::getFunctionSet().size(), 1u);
  EXPECT_EQ(neug::extension::ldbc_ic::IS5Function::getFunctionSet().size(), 1u);
  EXPECT_EQ(neug::extension::ldbc_ic::IS6Function::getFunctionSet().size(), 1u);
  EXPECT_EQ(neug::extension::ldbc_ic::IS7Function::getFunctionSet().size(), 1u);
}

#ifdef WIGGLE_EXTENSION_LIB
namespace {

void setup_is_mini_graph(neug::Connection* conn) {
  auto run = [&](const std::string& q) {
    auto res = conn->Query(q);
    ASSERT_TRUE(res.has_value()) << res.error().ToString() << " query: " << q;
  };
  run("CREATE NODE TABLE PERSON(id INT64, firstName STRING, lastName STRING, "
      "birthday DATE, locationIP STRING, browserUsed STRING, gender STRING, "
      "creationDate TIMESTAMP, PRIMARY KEY(id));");
  run("CREATE NODE TABLE PLACE(id INT64, PRIMARY KEY(id));");
  run("CREATE NODE TABLE POST(id INT64, imageFile STRING, content STRING, "
      "length INT32, creationDate TIMESTAMP, PRIMARY KEY(id));");
  run("CREATE NODE TABLE COMMENT(id INT64, content STRING, creationDate "
      "TIMESTAMP, PRIMARY KEY(id));");
  run("CREATE NODE TABLE FORUM(id INT64, title STRING, PRIMARY KEY(id));");
  run("CREATE REL TABLE KNOWS(FROM PERSON TO PERSON, creationDate TIMESTAMP);");
  run("CREATE REL TABLE ISLOCATEDIN(FROM PERSON TO PLACE);");
  run("CREATE REL TABLE HASCREATOR(FROM POST TO PERSON, FROM COMMENT TO "
      "PERSON, creationDate TIMESTAMP);");
  run("CREATE REL TABLE REPLYOF(FROM COMMENT TO POST, FROM COMMENT TO "
      "COMMENT);");
  run("CREATE REL TABLE CONTAINEROF(FROM FORUM TO POST);");
  run("CREATE REL TABLE HASMODERATOR(FROM FORUM TO PERSON);");

  run("CREATE (p:PERSON {id: 1, firstName: 'Alice', lastName: 'A', "
      "birthday: date('1990-01-02'), locationIP: '127.0.0.1', "
      "browserUsed: 'Chrome', gender: 'female', creationDate: "
      "timestamp('2010-01-01 00:00:00.000')}), "
      "(f:PERSON {id: 2, firstName: 'Bob', lastName: 'B', birthday: "
      "date('1991-03-04'), locationIP: '127.0.0.2', browserUsed: 'Firefox', "
      "gender: 'male', creationDate: timestamp('2010-02-01 00:00:00.000')}), "
      "(city:PLACE {id: 100});");
  run("MATCH (p:PERSON {id:1}), (city:PLACE {id:100}) "
      "CREATE (p)-[:ISLOCATEDIN]->(city);");
  run("MATCH (a:PERSON {id:1}), (b:PERSON {id:2}) "
      "CREATE (a)-[:KNOWS {creationDate: timestamp('2011-01-01 "
      "00:00:00.000')}]->(b);");

  run("CREATE (post:POST {id: 10, imageFile: '', content: 'root post', "
      "length: 9, creationDate: timestamp('2012-01-01 00:00:00.000')}), "
      "(comment:COMMENT {id: 20, content: 'reply text', creationDate: "
      "timestamp('2012-02-01 00:00:00.000')}), "
      "(forum:FORUM {id: 30, title: 'Test Forum'});");
  run("MATCH (post:POST {id:10}), (p:PERSON {id:1}) "
      "CREATE (post)-[:HASCREATOR {creationDate: timestamp('2012-01-01 "
      "00:00:00.000')}]->(p);");
  run("MATCH (comment:COMMENT {id:20}), (p:PERSON {id:2}) "
      "CREATE (comment)-[:HASCREATOR {creationDate: timestamp('2012-02-01 "
      "00:00:00.000')}]->(p);");
  run("MATCH (comment:COMMENT {id:20}), (post:POST {id:10}) "
      "CREATE (comment)-[:REPLYOF]->(post);");
  run("MATCH (forum:FORUM {id:30}), (post:POST {id:10}) "
      "CREATE (forum)-[:CONTAINEROF]->(post);");
  run("MATCH (forum:FORUM {id:30}), (p:PERSON {id:1}) "
      "CREATE (forum)-[:HASMODERATOR]->(p);");
}

}  // namespace

TEST(ISFunctions, CallSmokeTests) {
  const std::string ext_path = WIGGLE_EXTENSION_LIB;
  ASSERT_TRUE(std::filesystem::exists(ext_path)) << ext_path;

  const std::string db_path = "/tmp/is_call_smoke_test";
  std::filesystem::remove_all(db_path);

  neug::NeugDB db;
  ASSERT_TRUE(db.Open(db_path));
  auto conn = db.Connect();
  ASSERT_NE(conn, nullptr);
  setup_is_mini_graph(conn.get());

  auto load_res = conn->Query("LOAD '" + ext_path + "'");
  ASSERT_TRUE(load_res.has_value()) << load_res.error().ToString();

  auto is1_res = conn->Query(
      "CALL is1(1) YIELD firstName, lastName, birthday, locationIp, "
      "browserUsed, cityId, gender, creationDate "
      "RETURN firstName, cityId, gender");
  ASSERT_TRUE(is1_res.has_value()) << is1_res.error().ToString();
  ASSERT_EQ(is1_res->length(), 1u);
  is1_res->Reset();
  ASSERT_TRUE(is1_res->hasNext());
  EXPECT_EQ(is1_res->GetString(0), "Alice");
  EXPECT_EQ(is1_res->GetInt64(1), 100);
  EXPECT_EQ(is1_res->GetString(2), "female");

  auto is2_res = conn->Query(
      "CALL is2(1) YIELD messageId, messageContent, messageCreationDate, "
      "originalPostId, originalPostAuthorId, originalPostAuthorFirstName, "
      "originalPostAuthorLastName RETURN messageId, originalPostAuthorId");
  ASSERT_TRUE(is2_res.has_value()) << is2_res.error().ToString();
  ASSERT_EQ(is2_res->length(), 1u);
  is2_res->Reset();
  ASSERT_TRUE(is2_res->hasNext());
  EXPECT_EQ(is2_res->GetInt64(0), 10);
  EXPECT_EQ(is2_res->GetInt64(1), 1);

  auto is3_res = conn->Query(
      "CALL is3(1) YIELD personId, firstName, lastName, "
      "friendshipCreationDate RETURN personId, firstName");
  ASSERT_TRUE(is3_res.has_value()) << is3_res.error().ToString();
  ASSERT_EQ(is3_res->length(), 1u);
  is3_res->Reset();
  ASSERT_TRUE(is3_res->hasNext());
  EXPECT_EQ(is3_res->GetInt64(0), 2);
  EXPECT_EQ(is3_res->GetString(1), "Bob");

  auto is4_res = conn->Query(
      "CALL is4(10) YIELD messageContent, messageCreationDate "
      "RETURN messageContent");
  ASSERT_TRUE(is4_res.has_value()) << is4_res.error().ToString();
  ASSERT_EQ(is4_res->length(), 1u);
  is4_res->Reset();
  ASSERT_TRUE(is4_res->hasNext());
  EXPECT_EQ(is4_res->GetString(0), "root post");

  auto is5_res = conn->Query(
      "CALL is5(20) YIELD personId, firstName, lastName RETURN personId, "
      "firstName");
  ASSERT_TRUE(is5_res.has_value()) << is5_res.error().ToString();
  ASSERT_EQ(is5_res->length(), 1u);
  is5_res->Reset();
  ASSERT_TRUE(is5_res->hasNext());
  EXPECT_EQ(is5_res->GetInt64(0), 2);
  EXPECT_EQ(is5_res->GetString(1), "Bob");

  auto is6_res = conn->Query(
      "CALL is6(20) YIELD forumId, forumTitle, moderatorId, "
      "moderatorFirstName, moderatorLastName RETURN forumId, forumTitle, "
      "moderatorFirstName");
  ASSERT_TRUE(is6_res.has_value()) << is6_res.error().ToString();
  ASSERT_EQ(is6_res->length(), 1u);
  is6_res->Reset();
  ASSERT_TRUE(is6_res->hasNext());
  EXPECT_EQ(is6_res->GetInt64(0), 30);
  EXPECT_EQ(is6_res->GetString(1), "Test Forum");
  EXPECT_EQ(is6_res->GetString(2), "Alice");

  auto is7_res = conn->Query(
      "CALL is7(10) YIELD commentId, commentContent, commentCreationDate, "
      "replyAuthorId, replyAuthorFirstName, replyAuthorLastName, "
      "isReplyAuthorKnowsOriginalMessageAuthor "
      "RETURN commentId, replyAuthorId, "
      "isReplyAuthorKnowsOriginalMessageAuthor");
  ASSERT_TRUE(is7_res.has_value()) << is7_res.error().ToString();
  ASSERT_EQ(is7_res->length(), 1u);
  is7_res->Reset();
  ASSERT_TRUE(is7_res->hasNext());
  EXPECT_EQ(is7_res->GetInt64(0), 20);
  EXPECT_EQ(is7_res->GetInt64(1), 2);
  EXPECT_TRUE(is7_res->GetBool(2));

  conn.reset();
  db.Close();
  std::filesystem::remove_all(db_path);
}
#endif
