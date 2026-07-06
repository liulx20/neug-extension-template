/**
 * Copyright 2020 Alibaba Group Holding Limited.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ic10.h"

#include <queue>
#include <vector>

#include "ldbc_common.h"
#include "neug/execution/common/columns/value_columns.h"
#include "neug/utils/exception/exception.h"

namespace neug {
namespace extension {
namespace ldbc_ic {
namespace {

constexpr size_t kTopN = 10;

struct PersonResult {
  int score = 0;
  vid_t person_vid = 0;
  int64_t person_id = 0;
};

struct PersonResultComparer {
  bool operator()(const PersonResult& lhs, const PersonResult& rhs) const {
    if (lhs.score > rhs.score) {
      return true;
    }
    if (lhs.score < rhs.score) {
      return false;
    }
    return lhs.person_id < rhs.person_id;
  }
};

bool matches_zodiac_month(const Date& birthday, int month) {
  const int this_month = month;
  const int next_month = (month == 12) ? 1 : (month + 1);
  return (birthday.month() == this_month && birthday.day() >= 21) ||
         (birthday.month() == next_month && birthday.day() < 22);
}

int score_friend_posts(const StorageReadInterface& graph, vid_t friend_vid,
                       const CsrView& post_has_creator_in,
                       const CsrView& post_has_tag_out,
                       const std::vector<bool>& has_interests) {
  int score = 0;
  const auto posts = post_has_creator_in.get_edges(friend_vid);
  for (auto pit = posts.begin(); pit != posts.end(); ++pit) {
    const vid_t post_vid = *pit;
    int diff = -1;
    const auto tags = post_has_tag_out.get_edges(post_vid);
    for (auto tit = tags.begin(); tit != tags.end(); ++tit) {
      if (*tit < has_interests.size() && has_interests[*tit]) {
        diff = 1;
        break;
      }
    }
    score += diff;
  }
  return score;
}

std::unique_ptr<function::CallFuncInputBase> bind_ic10(
    const Schema& /*schema*/, const execution::ContextMeta& /*ctx_meta*/,
    const ::physical::PhysicalPlan& plan, int op_idx) {
  const auto& params =
      plan.plan(op_idx).opr().procedure_call().query().arguments();
  if (params.size() < 2) {
    THROW_INVALID_ARGUMENT_EXCEPTION(
        "ic10 requires 2 arguments: personId and month");
  }
  auto input = std::make_unique<IC10FuncInput>();
  ldbc::bind_ldbc_call(plan, op_idx, input.get());
  return input;
}

execution::Context exec_ic10(const function::CallFuncInputBase& input,
                             IStorageInterface& graph_iface,
                             const execution::ParamsMap& params) {
  const auto& args = dynamic_cast<const IC10FuncInput&>(input);
  const int64_t person_id = params.at("personId").GetValue<int64_t>();
  const int32_t month = params.at("month").GetValue<int32_t>();
  const auto& graph = dynamic_cast<const StorageReadInterface&>(graph_iface);
  const auto& schema = graph.schema();

  const label_t person_label = schema.get_vertex_label_id("PERSON");
  const label_t place_label = schema.get_vertex_label_id("PLACE");
  const label_t tag_label = schema.get_vertex_label_id("TAG");
  const label_t post_label = schema.get_vertex_label_id("POST");
  const label_t knows_label = schema.get_edge_label_id("KNOWS");
  const label_t has_interest_label = schema.get_edge_label_id("HASINTEREST");
  const label_t has_creator_label = schema.get_edge_label_id("HASCREATOR");
  const label_t has_tag_label = schema.get_edge_label_id("HASTAG");
  const label_t is_located_in_label = schema.get_edge_label_id("ISLOCATEDIN");

  auto first_name_col = ldbc::get_vertex_column<std::string_view>(
      graph, person_label, "firstName");
  auto last_name_col = ldbc::get_vertex_column<std::string_view>(
      graph, person_label, "lastName");
  auto gender_col =
      ldbc::get_vertex_column<std::string_view>(graph, person_label, "gender");
  auto birthday_col =
      ldbc::get_vertex_column<Date>(graph, person_label, "birthday");
  auto place_name_col =
      ldbc::get_vertex_column<std::string_view>(graph, place_label, "name");
  auto person_id_col = ldbc::get_vertex_column<int64_t>(graph, person_label, "id");
  if (!first_name_col || !last_name_col || !gender_col || !birthday_col ||
      !place_name_col) {
    THROW_RUNTIME_ERROR("ic10: failed to load required LDBC property columns");
  }

  vid_t root = StorageReadInterface::kInvalidVid;
  if (!graph.GetVertexIndex(person_label, execution::Value::INT64(person_id),
                            root)) {
    return execution::Context{};
  }

  const auto has_interest_out = graph.GetGenericOutgoingGraphView(
      person_label, tag_label, has_interest_label);
  std::vector<bool> has_interests(graph.GetVertexSet(tag_label).size(), false);
  const auto interests = has_interest_out.get_edges(root);
  for (auto it = interests.begin(); it != interests.end(); ++it) {
    has_interests[*it] = true;
  }

  std::vector<vid_t> friends;
  ldbc::foreach_knows_2d_neighbor(
      graph, person_label, knows_label, root, [&](vid_t v) {
        if (matches_zodiac_month(birthday_col->get_view(v), month)) {
          friends.push_back(v);
        }
      });

  const auto post_has_creator_in = graph.GetGenericIncomingGraphView(
      person_label, post_label, has_creator_label);
  const auto post_has_tag_out =
      graph.GetGenericOutgoingGraphView(post_label, tag_label, has_tag_label);
  const auto person_located_in_out = graph.GetGenericOutgoingGraphView(
      person_label, place_label, is_located_in_label);

  std::priority_queue<PersonResult, std::vector<PersonResult>,
                      PersonResultComparer>
      pq;
  size_t friends_index = 0;
  const size_t step_1_size = std::min(friends.size(), static_cast<size_t>(10));
  while (friends_index < step_1_size) {
    const vid_t v = friends[friends_index++];
    const int score = score_friend_posts(graph, v, post_has_creator_in,
                                         post_has_tag_out, has_interests);
    pq.push(PersonResult{
        score, v, person_id_col->get_view(v)});
  }

  int min_score = pq.empty() ? 0 : pq.top().score;
  while (friends_index < friends.size()) {
    const vid_t v = friends[friends_index++];
    int score = 0;
    const auto posts = post_has_creator_in.get_edges(v);
    size_t remaining = 0;
    for (auto pit = posts.begin(); pit != posts.end(); ++pit) {
      ++remaining;
    }
    for (auto pit = posts.begin(); pit != posts.end(); ++pit) {
      if (score + static_cast<int>(remaining) < min_score) {
        break;
      }
      int diff = -1;
      const auto tags = post_has_tag_out.get_edges(*pit);
      for (auto tit = tags.begin(); tit != tags.end(); ++tit) {
        if (*tit < has_interests.size() && has_interests[*tit]) {
          diff = 1;
          break;
        }
      }
      score += diff;
      --remaining;
    }
    if (pq.size() < kTopN) {
      pq.push(PersonResult{
          score, v, person_id_col->get_view(v)});
      min_score = pq.top().score;
    } else if (score > min_score) {
      pq.pop();
      pq.push(PersonResult{
          score, v, person_id_col->get_view(v)});
      min_score = pq.top().score;
    } else if (score == min_score) {
      const int64_t person_id =
          person_id_col->get_view(v);
      if (person_id < pq.top().person_id) {
        pq.pop();
        pq.push(PersonResult{score, v, person_id});
      }
    }
  }

  std::vector<PersonResult> results;
  results.reserve(pq.size());
  while (!pq.empty()) {
    results.push_back(pq.top());
    pq.pop();
  }

  execution::ValueColumnBuilder<int64_t> person_id_builder;
  execution::ValueColumnBuilder<std::string> first_name_builder;
  execution::ValueColumnBuilder<std::string> last_name_builder;
  execution::ValueColumnBuilder<int32_t> score_builder;
  execution::ValueColumnBuilder<std::string> gender_builder;
  execution::ValueColumnBuilder<std::string> city_builder;

  for (size_t i = results.size(); i > 0; --i) {
    const auto& row = results[i - 1];
    person_id_builder.push_back_opt(row.person_id);
    first_name_builder.push_back_opt(
        std::string(first_name_col->get_view(row.person_vid)));
    last_name_builder.push_back_opt(
        std::string(last_name_col->get_view(row.person_vid)));
    score_builder.push_back_opt(row.score);
    gender_builder.push_back_opt(
        std::string(gender_col->get_view(row.person_vid)));
    const vid_t place_vid =
        ldbc::get_single_out_neighbor(person_located_in_out, row.person_vid);
    if (place_vid != StorageReadInterface::kInvalidVid) {
      city_builder.push_back_opt(
          std::string(place_name_col->get_view(place_vid)));
    } else {
      city_builder.push_back_null();
    }
  }

  return ldbc::make_output_context(
      args.output_aliases,
      {person_id_builder.finish(), first_name_builder.finish(),
       last_name_builder.finish(), score_builder.finish(),
       gender_builder.finish(), city_builder.finish()});
}

}  // namespace

function::function_set IC10Function::getFunctionSet() {
  auto fn = std::make_unique<function::NeugCallFunction>(
      IC10Function::name,
      std::vector<common::DataTypeId>{common::DataTypeId::kInt64,
                                      common::DataTypeId::kInt64},
      function::call_output_columns{
          {"personId", common::DataTypeId::kInt64},
          {"personFirstName", common::DataTypeId::kVarchar},
          {"personLastName", common::DataTypeId::kVarchar},
          {"score", common::DataTypeId::kInt32},
          {"personGender", common::DataTypeId::kVarchar},
          {"cityName", common::DataTypeId::kVarchar}});
  fn->bindFunc = bind_ic10;
  fn->execFunc = exec_ic10;
  function::function_set set;
  set.push_back(std::move(fn));
  return set;
}

}  // namespace ldbc_ic
}  // namespace extension
}  // namespace neug
