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

#include "ic1.h"
#include "ic4.h"
#include "ic5.h"
#include "ic6.h"
#include "ic7.h"
#include "ic8.h"
#include "ic9.h"
#include "ic10.h"
#include "ic11.h"
#include "ic12.h"
#include "ic13.h"
#include "ic14.h"
#include "neug/main/neug_db.h"
#include "neug/utils/property/types.h"

TEST(ICFunctions, FunctionSets) {
  EXPECT_EQ(neug::extension::ldbc_ic::IC1Function::getFunctionSet().size(), 1u);
  EXPECT_EQ(neug::extension::ldbc_ic::IC4Function::getFunctionSet().size(), 1u);
  EXPECT_EQ(neug::extension::ldbc_ic::IC5Function::getFunctionSet().size(), 1u);
  EXPECT_EQ(neug::extension::ldbc_ic::IC6Function::getFunctionSet().size(), 1u);
  EXPECT_EQ(neug::extension::ldbc_ic::IC7Function::getFunctionSet().size(), 1u);
  EXPECT_EQ(neug::extension::ldbc_ic::IC8Function::getFunctionSet().size(), 1u);
  EXPECT_EQ(neug::extension::ldbc_ic::IC9Function::getFunctionSet().size(), 1u);
  EXPECT_EQ(neug::extension::ldbc_ic::IC10Function::getFunctionSet().size(), 1u);
  EXPECT_EQ(neug::extension::ldbc_ic::IC11Function::getFunctionSet().size(), 1u);
  EXPECT_EQ(neug::extension::ldbc_ic::IC12Function::getFunctionSet().size(), 1u);
  EXPECT_EQ(neug::extension::ldbc_ic::IC13Function::getFunctionSet().size(), 1u);
  EXPECT_EQ(neug::extension::ldbc_ic::IC14Function::getFunctionSet().size(), 1u);
}

#ifdef WIGGLE_EXTENSION_LIB
namespace {

void setup_ic_mini_graph(neug::Connection* conn) {
  auto run = [&](const std::string& q) {
    auto res = conn->Query(q);
    ASSERT_TRUE(res.has_value()) << res.error().ToString() << " query: " << q;
  };
  run("CREATE NODE TABLE PERSON(id INT64, firstName STRING, lastName STRING, "
      "gender STRING, birthday DATE, creationDate TIMESTAMP, browserUsed "
      "STRING, locationIP STRING, email STRING, language STRING, "
      "PRIMARY KEY(id));");
  run("CREATE NODE TABLE POST(id INT64, imageFile STRING, content STRING, "
      "length INT32, creationDate TIMESTAMP, PRIMARY KEY(id));");
  run("CREATE NODE TABLE COMMENT(id INT64, content STRING, creationDate "
      "TIMESTAMP, PRIMARY KEY(id));");
  run("CREATE NODE TABLE TAG(id INT64, name STRING, PRIMARY KEY(id));");
  run("CREATE NODE TABLE TAGCLASS(id INT64, name STRING, PRIMARY KEY(id));");
  run("CREATE NODE TABLE FORUM(id INT64, title STRING, PRIMARY KEY(id));");
  run("CREATE NODE TABLE PLACE(id INT64, name STRING, PRIMARY KEY(id));");
  run("CREATE NODE TABLE ORGANISATION(id INT64, name STRING, "
      "PRIMARY KEY(id));");
  run("CREATE REL TABLE KNOWS(FROM PERSON TO PERSON);");
  run("CREATE REL TABLE HASCREATOR(FROM POST TO PERSON, FROM COMMENT TO "
      "PERSON, creationDate TIMESTAMP);");
  run("CREATE REL TABLE HASTAG(FROM POST TO TAG);");
  run("CREATE REL TABLE HASTYPE(FROM TAG TO TAGCLASS);");
  run("CREATE REL TABLE ISSUBCLASSOF(FROM TAGCLASS TO TAGCLASS);");
  run("CREATE REL TABLE HASINTEREST(FROM PERSON TO TAG);");
  run("CREATE REL TABLE ISLOCATEDIN(FROM PERSON TO PLACE, FROM ORGANISATION "
      "TO PLACE);");
  run("CREATE REL TABLE WORKAT(FROM PERSON TO ORGANISATION, workFrom INT32);");
  run("CREATE REL TABLE STUDYAT(FROM PERSON TO ORGANISATION, classYear INT32);");
  run("CREATE REL TABLE HASMEMBER(FROM FORUM TO PERSON, joinDate TIMESTAMP);");
  run("CREATE REL TABLE CONTAINEROF(FROM FORUM TO POST);");
  run("CREATE REL TABLE LIKES(FROM PERSON TO POST, FROM PERSON TO COMMENT, "
      "creationDate TIMESTAMP);");
  run("CREATE REL TABLE REPLYOF(FROM COMMENT TO POST, FROM COMMENT TO "
      "COMMENT);");

  run("CREATE (root:PERSON {id: 1, firstName: 'Alice', lastName: 'A', gender: "
      "'female', birthday: date('1980-01-01'), creationDate: "
      "timestamp('2010-01-01 00:00:00.000'), browserUsed: 'Chrome', "
      "locationIP: '1.1.1.1', email: 'alice@test.com', language: 'en'}), "
      "(friend:PERSON {id: 2, firstName: 'Bob', lastName: 'B', gender: "
      "'male', birthday: date('1985-03-01'), creationDate: "
      "timestamp('2010-02-01 00:00:00.000'), browserUsed: 'Firefox', "
      "locationIP: '2.2.2.2', email: 'bob@test.com', language: 'de'}), "
      "(liker:PERSON {id: 3, firstName: 'Carol', lastName: 'C', gender: "
      "'female', birthday: date('1988-07-01'), creationDate: "
      "timestamp('2010-03-01 00:00:00.000'), browserUsed: 'Safari', "
      "locationIP: '3.3.3.3', email: 'carol@test.com', language: 'en'}), "
      "(replier:PERSON {id: 4, firstName: 'Dan', lastName: 'D', gender: "
      "'male', birthday: date('1990-11-01'), creationDate: "
      "timestamp('2010-04-01 00:00:00.000'), browserUsed: 'Edge', "
      "locationIP: '4.4.4.4', email: 'dan@test.com', language: 'fr'}), "
      "(foaf:PERSON {id: 5, firstName: 'Eve', lastName: 'E', gender: "
      "'female', birthday: date('1990-06-10'), creationDate: "
      "timestamp('2010-05-01 00:00:00.000'), browserUsed: 'Chrome', "
      "locationIP: '5.5.5.5', email: 'eve@test.com', language: 'en'}), "
      "(tag1:TAG {id: 10, name: 'Music'}), "
      "(tag2:TAG {id: 11, name: 'Sports'}), "
      "(tag3:TAG {id: 12, name: 'Joseph_Goebbels'}), "
      "(tagclass:TAGCLASS {id: 50, name: 'Chancellor'}), "
      "(forum:FORUM {id: 20, title: 'Test Forum'}), "
      "(city:PLACE {id: 30, name: 'Zurich'}), "
      "(country:PLACE {id: 31, name: 'Switzerland'}), "
      "(company:ORGANISATION {id: 40, name: 'TestCo'}), "
      "(university:ORGANISATION {id: 41, name: 'TestUni'});");
  run("MATCH (a:PERSON {id:1}), (b:PERSON {id:2}) CREATE (a)-[:KNOWS]->(b);");
  run("MATCH (b:PERSON {id:2}), (c:PERSON {id:5}) CREATE (b)-[:KNOWS]->(c);");
  run("MATCH (root:PERSON {id:1}), (city:PLACE {id:30}) "
      "CREATE (root)-[:ISLOCATEDIN]->(city);");
  run("MATCH (foaf:PERSON {id:5}), (city:PLACE {id:30}) "
      "CREATE (foaf)-[:ISLOCATEDIN]->(city);");
  run("MATCH (foaf:PERSON {id:5}), (tag1:TAG {id:10}) "
      "CREATE (foaf)-[:HASINTEREST]->(tag1);");
  run("MATCH (root:PERSON {id:1}), (tag1:TAG {id:10}) "
      "CREATE (root)-[:HASINTEREST]->(tag1);");
  run("MATCH (company:ORGANISATION {id:40}), (country:PLACE {id:31}) "
      "CREATE (company)-[:ISLOCATEDIN]->(country);");
  run("MATCH (friend:PERSON {id:2}), (company:ORGANISATION {id:40}) "
      "CREATE (friend)-[:WORKAT {workFrom: 2003}]->(company);");
  run("MATCH (friend:PERSON {id:2}), (city:PLACE {id:30}) "
      "CREATE (friend)-[:ISLOCATEDIN]->(city);");
  run("MATCH (university:ORGANISATION {id:41}), (city:PLACE {id:30}) "
      "CREATE (university)-[:ISLOCATEDIN]->(city);");
  run("MATCH (friend:PERSON {id:2}), (university:ORGANISATION {id:41}) "
      "CREATE (friend)-[:STUDYAT {classYear: 2001}]->(university);");
  run("MATCH (tag3:TAG {id:12}), (tagclass:TAGCLASS {id:50}) "
      "CREATE (tag3)-[:HASTYPE]->(tagclass);");
  run("CREATE (post:POST {id: 100, imageFile: '', content: 'hello', length: 5, "
      "creationDate: timestamp('2012-06-10 00:00:00.000')}), "
      "(post2:POST {id: 101, imageFile: '', content: 'tagged', length: 6, "
      "creationDate: timestamp('2012-06-15 00:00:00.000')}), "
      "(post3:POST {id: 102, imageFile: '', content: 'foaf post', length: 9, "
      "creationDate: timestamp('2012-06-16 00:00:00.000')}), "
      "(comment:COMMENT {id: 200, content: 'reply', creationDate: "
      "timestamp('2012-06-20 00:00:00.000')}), "
      "(reply:COMMENT {id: 201, content: 'nested reply', creationDate: "
      "timestamp('2012-06-21 00:00:00.000')}), "
      "(friend_comment:COMMENT {id: 202, content: 'friend reply', "
      "creationDate: timestamp('2012-06-22 00:00:00.000')});");
  run("MATCH (post:POST {id:100}), (root:PERSON {id:1}) "
      "CREATE (post)-[:HASCREATOR {creationDate: timestamp('2012-06-10 "
      "00:00:00.000')}]->(root);");
  run("MATCH (post2:POST {id:101}), (friend:PERSON {id:2}) "
      "CREATE (post2)-[:HASCREATOR {creationDate: timestamp('2012-06-15 "
      "00:00:00.000')}]->(friend);");
  run("MATCH (post3:POST {id:102}), (foaf:PERSON {id:5}) "
      "CREATE (post3)-[:HASCREATOR {creationDate: timestamp('2012-06-16 "
      "00:00:00.000')}]->(foaf);");
  run("MATCH (comment:COMMENT {id:200}), (root:PERSON {id:1}) "
      "CREATE (comment)-[:HASCREATOR {creationDate: timestamp('2012-06-20 "
      "00:00:00.000')}]->(root);");
  run("MATCH (reply:COMMENT {id:201}), (replier:PERSON {id:4}) "
      "CREATE (reply)-[:HASCREATOR {creationDate: timestamp('2012-06-21 "
      "00:00:00.000')}]->(replier);");
  run("MATCH (post2:POST {id:101}), (tag1:TAG {id:10}), (tag2:TAG {id:11}), "
      "(tag3:TAG {id:12}) "
      "CREATE (post2)-[:HASTAG]->(tag1), (post2)-[:HASTAG]->(tag2), "
      "(post2)-[:HASTAG]->(tag3);");
  run("MATCH (post3:POST {id:102}), (tag1:TAG {id:10}) "
      "CREATE (post3)-[:HASTAG]->(tag1);");
  run("MATCH (comment:COMMENT {id:200}), (post:POST {id:100}) "
      "CREATE (comment)-[:REPLYOF]->(post);");
  run("MATCH (reply:COMMENT {id:201}), (comment:COMMENT {id:200}) "
      "CREATE (reply)-[:REPLYOF]->(comment);");
  run("MATCH (friend_comment:COMMENT {id:202}), (post2:POST {id:101}) "
      "CREATE (friend_comment)-[:REPLYOF]->(post2);");
  run("MATCH (friend_comment:COMMENT {id:202}), (friend:PERSON {id:2}) "
      "CREATE (friend_comment)-[:HASCREATOR {creationDate: timestamp('2012-06-22 "
      "00:00:00.000')}]->(friend);");
  run("MATCH (forum:FORUM {id:20}), (friend:PERSON {id:2}) "
      "CREATE (forum)-[:HASMEMBER {joinDate: timestamp('2012-09-10 "
      "00:00:00.000')}]->(friend);");
  run("MATCH (forum:FORUM {id:20}), (post2:POST {id:101}) "
      "CREATE (forum)-[:CONTAINEROF]->(post2);");
  run("MATCH (liker:PERSON {id:3}), (post:POST {id:100}) "
      "CREATE (liker)-[:LIKES {creationDate: timestamp('2012-06-25 "
      "00:00:00.000')}]->(post);");
}

}  // namespace

TEST(ICFunctions, CallSmokeTests) {
  const std::string ext_path = WIGGLE_EXTENSION_LIB;
  ASSERT_TRUE(std::filesystem::exists(ext_path)) << ext_path;

  const std::string db_path = "/tmp/ic_call_smoke_test";
  std::filesystem::remove_all(db_path);

  neug::NeugDB db;
  ASSERT_TRUE(db.Open(db_path));
  auto conn = db.Connect();
  ASSERT_NE(conn, nullptr);
  setup_ic_mini_graph(conn.get());

  auto load_res = conn->Query("LOAD '" + ext_path + "'");
  ASSERT_TRUE(load_res.has_value()) << load_res.error().ToString();

  const int64_t start_ms =
      neug::DateTime("2012-06-01 00:00:00.000").milli_second;
  auto ic1_res = conn->Query(
      "CALL ic1(1, 'Bob') "
      "YIELD friendId, distanceFromPerson, friendLastName, friendCityName, "
      "friendCompanies, friendUniversities "
      "RETURN friendId, distanceFromPerson, friendLastName, friendCityName, "
      "friendCompanies, friendUniversities",
      "read");
  ASSERT_TRUE(ic1_res.has_value()) << ic1_res.error().ToString();
  ASSERT_EQ(ic1_res->length(), 1u);
  ic1_res->Reset();
  ASSERT_TRUE(ic1_res->hasNext());
  EXPECT_EQ(ic1_res->GetInt64(0), 2);
  EXPECT_EQ(ic1_res->GetInt32(1), 1);
  EXPECT_EQ(ic1_res->GetString(2), "B");
  EXPECT_EQ(ic1_res->GetString(3), "Zurich");

  auto ic4_res = conn->Query(
      "CALL ic4(1, " + std::to_string(start_ms) + ", 28) "
      "YIELD tagName, postCount RETURN tagName, postCount",
      "read");
  ASSERT_TRUE(ic4_res.has_value()) << ic4_res.error().ToString();
  ASSERT_EQ(ic4_res->length(), 3u);
  ic4_res->Reset();
  bool found_sports = false;
  while (ic4_res->hasNext()) {
    if (ic4_res->GetString(0) == "Sports" && ic4_res->GetInt32(1) == 1) {
      found_sports = true;
    }
    ic4_res->next();
  }
  EXPECT_TRUE(found_sports);

  const int64_t min_date_ms =
      neug::DateTime("2012-09-02 00:00:00.000").milli_second;
  auto ic5_res = conn->Query(
      "CALL ic5(1, " + std::to_string(min_date_ms) + ") "
      "YIELD forumTitle, postCount RETURN forumTitle, postCount",
      "read");
  ASSERT_TRUE(ic5_res.has_value()) << ic5_res.error().ToString();
  ASSERT_EQ(ic5_res->length(), 1u);
  ic5_res->Reset();
  ASSERT_TRUE(ic5_res->hasNext());
  EXPECT_EQ(ic5_res->GetString(0), "Test Forum");
  EXPECT_EQ(ic5_res->GetInt32(1), 1);

  auto ic6_res = conn->Query(
      "CALL ic6(1, 'Music') YIELD tagName, postCount RETURN tagName, postCount",
      "read");
  ASSERT_TRUE(ic6_res.has_value()) << ic6_res.error().ToString();
  ASSERT_EQ(ic6_res->length(), 2u);
  ic6_res->Reset();
  bool found_sports_ic6 = false;
  while (ic6_res->hasNext()) {
    if (ic6_res->GetString(0) == "Sports" && ic6_res->GetInt32(1) == 1) {
      found_sports_ic6 = true;
    }
    ic6_res->next();
  }
  EXPECT_TRUE(found_sports_ic6);

  auto ic7_res = conn->Query(
      "CALL ic7(1) YIELD personId, personFirstName, personLastName, "
      "likeCreationDate, messageId, messageContent, minutesLatency, isNew "
      "RETURN personId, isNew",
      "read");
  ASSERT_TRUE(ic7_res.has_value()) << ic7_res.error().ToString();
  ASSERT_EQ(ic7_res->length(), 1u);
  ic7_res->Reset();
  ASSERT_TRUE(ic7_res->hasNext());
  EXPECT_EQ(ic7_res->GetInt64(0), 3);
  EXPECT_TRUE(ic7_res->GetBool(1));

  auto ic8_res = conn->Query(
      "CALL ic8(1) YIELD personId, personFirstName, personLastName, "
      "commentCreationDate, commentId, commentContent "
      "RETURN personId, commentId, commentContent",
      "read");
  ASSERT_TRUE(ic8_res.has_value()) << ic8_res.error().ToString();
  ASSERT_GE(ic8_res->length(), 1u);
  ic8_res->Reset();
  ASSERT_TRUE(ic8_res->hasNext());
  EXPECT_EQ(ic8_res->GetInt64(0), 4);
  EXPECT_EQ(ic8_res->GetInt64(1), 201);
  EXPECT_EQ(ic8_res->GetString(2), "nested reply");

  const int64_t max_date_ms =
      neug::DateTime("2012-07-01 00:00:00.000").milli_second;
  auto ic9_res = conn->Query(
      "CALL ic9(1, " + std::to_string(max_date_ms) + ") "
      "YIELD personId, messageId RETURN personId, messageId",
      "read");
  ASSERT_TRUE(ic9_res.has_value()) << ic9_res.error().ToString();
  ASSERT_GE(ic9_res->length(), 1u);
  ic9_res->Reset();
  ASSERT_TRUE(ic9_res->hasNext());
  EXPECT_EQ(ic9_res->GetInt64(0), 2);
  EXPECT_EQ(ic9_res->GetInt64(1), 202);

  auto ic10_res = conn->Query(
      "CALL ic10(1, 5) YIELD personId, score RETURN personId, score", "read");
  ASSERT_TRUE(ic10_res.has_value()) << ic10_res.error().ToString();
  ASSERT_GE(ic10_res->length(), 1u);
  ic10_res->Reset();
  ASSERT_TRUE(ic10_res->hasNext());
  EXPECT_EQ(ic10_res->GetInt64(0), 5);
  EXPECT_EQ(ic10_res->GetInt32(1), 1);

  auto ic11_res = conn->Query(
      "CALL ic11(1, 'Switzerland', 2006) "
      "YIELD personId, organizationName, organizationWorkFromYear "
      "RETURN personId, organizationName, organizationWorkFromYear",
      "read");
  ASSERT_TRUE(ic11_res.has_value()) << ic11_res.error().ToString();
  ASSERT_EQ(ic11_res->length(), 1u);
  ic11_res->Reset();
  ASSERT_TRUE(ic11_res->hasNext());
  EXPECT_EQ(ic11_res->GetInt64(0), 2);
  EXPECT_EQ(ic11_res->GetString(1), "TestCo");
  EXPECT_EQ(ic11_res->GetInt32(2), 2003);

  auto ic12_res = conn->Query(
      "CALL ic12(1, 'Chancellor') "
      "YIELD personId, replyCount RETURN personId, replyCount",
      "read");
  ASSERT_TRUE(ic12_res.has_value()) << ic12_res.error().ToString();
  ASSERT_GE(ic12_res->length(), 1u);
  ic12_res->Reset();
  ASSERT_TRUE(ic12_res->hasNext());
  EXPECT_EQ(ic12_res->GetInt64(0), 2);
  EXPECT_EQ(ic12_res->GetInt32(1), 1);

  auto ic13_res = conn->Query(
      "CALL ic13(1, 2) YIELD shortestPathLength RETURN shortestPathLength",
      "read");
  ASSERT_TRUE(ic13_res.has_value()) << ic13_res.error().ToString();
  ASSERT_EQ(ic13_res->length(), 1u);
  ic13_res->Reset();
  ASSERT_TRUE(ic13_res->hasNext());
  EXPECT_EQ(ic13_res->GetInt32(0), 1);

  auto ic14_res = conn->Query(
      "CALL ic14(1, 2) YIELD pathWeight RETURN pathWeight", "read");
  ASSERT_TRUE(ic14_res.has_value()) << ic14_res.error().ToString();
  ASSERT_GE(ic14_res->length(), 1u);

  conn.reset();
  db.Close();
  std::filesystem::remove_all(db_path);
}
#endif
