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

#include "ic7.h"

#include <algorithm>
#include <array>
#include <queue>
#include <vector>

#include "ldbc_common.h"
#include "neug/execution/common/columns/value_columns.h"
#include "neug/utils/exception/exception.h"

namespace neug {
namespace extension {
namespace ldbc_ic {
namespace {

constexpr size_t kTopN = 20;
constexpr size_t kNumOutputColumns = 8;
constexpr int64_t kMillisPerMinute = 60 * 1000L;

struct LikeResult {
  vid_t person_vid = 0;
  vid_t message_vid = 0;
  int64_t like_date_ms = 0;
  int64_t person_id = 0;
  int64_t message_id = 0;
  bool is_post = false;
};

struct LikeResultComparer {
  bool operator()(const LikeResult& lhs, const LikeResult& rhs) const {
    if (lhs.like_date_ms > rhs.like_date_ms) {
      return true;
    }
    if (lhs.like_date_ms < rhs.like_date_ms) {
      return false;
    }
    return lhs.person_id < rhs.person_id;
  }
};

std::unique_ptr<function::CallFuncInputBase> bind_ic7(
    const Schema& /*schema*/, const execution::ContextMeta& /*ctx_meta*/,
    const ::physical::PhysicalPlan& plan, int op_idx) {
  const auto& params = plan.plan(op_idx).opr().procedure_call().query().arguments();
  if (params.size() < 1 || !params[0].has_const_()) {
    THROW_INVALID_ARGUMENT_EXCEPTION("ic7: personId must be an integer literal");
  }
  auto input = std::make_unique<IC7FuncInput>();
  input->person_id = ldbc::parse_i64_arg(params[0].const_(), "personId");
  ldbc::bind_output_aliases(plan, op_idx, &input->output_aliases);
  return input;
}

void collect_likes(
    const StorageReadInterface& graph, const Schema& schema, label_t person_label,
    label_t message_label, label_t likes_label, label_t has_creator_label,
    bool is_post, vid_t root,
    const StorageReadInterface::vertex_column_t<DateTime>* message_date_col,
    std::vector<LikeResult>* messages) {
  const auto message_has_creator_in = graph.GetGenericIncomingGraphView(
      person_label, message_label, has_creator_label);
  const auto person_likes_message_in = graph.GetGenericIncomingGraphView(
      message_label, person_label, likes_label);
  const bool has_like_date = schema.edge_has_property(
      person_label, message_label, likes_label, "creationDate");
  EdgeDataAccessor like_accessor;
  if (has_like_date) {
    like_accessor = graph.GetEdgeDataAccessor(person_label, message_label,
                                              likes_label, "creationDate");
  }
  const bool has_creator_date = schema.edge_has_property(
      message_label, person_label, has_creator_label, "creationDate");
  EdgeDataAccessor creator_accessor;
  if (has_creator_date) {
    creator_accessor = graph.GetEdgeDataAccessor(
        message_label, person_label, has_creator_label, "creationDate");
  }

  const auto root_messages = message_has_creator_in.get_edges(root);
  for (auto it = root_messages.begin(); it != root_messages.end(); ++it) {
    const vid_t message_vid = *it;
    const int64_t message_id =
        graph.GetVertexId(message_label, message_vid).GetValue<int64_t>();
    const auto likes = person_likes_message_in.get_edges(message_vid);
    for (auto like_it = likes.begin(); like_it != likes.end(); ++like_it) {
      LikeResult info;
      info.person_vid = *like_it;
      info.message_vid = message_vid;
      info.person_id =
          graph.GetVertexId(person_label, info.person_vid).GetValue<int64_t>();
      info.message_id = message_id;
      info.is_post = is_post;
      if (has_like_date) {
        info.like_date_ms =
            like_accessor.get_typed_data<DateTime>(like_it).milli_second;
      } else if (message_date_col) {
        info.like_date_ms = message_date_col->get_view(message_vid).milli_second;
      }
      messages->push_back(info);
    }
  }
}

execution::Context exec_ic7(const function::CallFuncInputBase& input,
                            IStorageInterface& graph_iface) {
  const auto& ic7_input = dynamic_cast<const IC7FuncInput&>(input);
  const auto& graph = dynamic_cast<const StorageReadInterface&>(graph_iface);
  const auto& schema = graph.schema();

  const label_t person_label = schema.get_vertex_label_id("PERSON");
  const label_t post_label = schema.get_vertex_label_id("POST");
  const label_t comment_label = schema.get_vertex_label_id("COMMENT");
  const label_t knows_label = schema.get_edge_label_id("KNOWS");
  const label_t likes_label = schema.get_edge_label_id("LIKES");
  const label_t has_creator_label = schema.get_edge_label_id("HASCREATOR");

  auto first_name_col =
      ldbc::get_vertex_column<std::string_view>(graph, person_label, "firstName");
  auto last_name_col =
      ldbc::get_vertex_column<std::string_view>(graph, person_label, "lastName");
  auto post_creation_date_col =
      ldbc::get_vertex_column<DateTime>(graph, post_label, "creationDate");
  auto comment_creation_date_col =
      ldbc::get_vertex_column<DateTime>(graph, comment_label, "creationDate");
  if (!first_name_col || !last_name_col) {
    THROW_RUNTIME_ERROR("ic7: failed to load required LDBC property columns");
  }

  vid_t root = StorageReadInterface::kInvalidVid;
  if (!graph.GetVertexIndex(person_label,
                            execution::Value::INT64(ic7_input.person_id),
                            root)) {
    return execution::Context{};
  }

  std::vector<bool> friends(graph.GetVertexSet(person_label).size(), false);
  const auto knows_out = graph.GetGenericOutgoingGraphView(
      person_label, person_label, knows_label);
  const auto knows_in = graph.GetGenericIncomingGraphView(
      person_label, person_label, knows_label);
  const auto kin = knows_in.get_edges(root);
  for (auto it = kin.begin(); it != kin.end(); ++it) {
    friends[*it] = true;
  }
  const auto kout = knows_out.get_edges(root);
  for (auto it = kout.begin(); it != kout.end(); ++it) {
    friends[*it] = true;
  }

  std::vector<LikeResult> messages;
  collect_likes(graph, schema, person_label, post_label, likes_label,
                has_creator_label, true, root, post_creation_date_col.get(),
                &messages);
  collect_likes(graph, schema, person_label, comment_label, likes_label,
                has_creator_label, false, root, comment_creation_date_col.get(),
                &messages);

  std::sort(messages.begin(), messages.end(),
            [](const LikeResult& lhs, const LikeResult& rhs) {
              if (lhs.person_id != rhs.person_id) {
                return lhs.person_id < rhs.person_id;
              }
              if (lhs.like_date_ms != rhs.like_date_ms) {
                return lhs.like_date_ms > rhs.like_date_ms;
              }
              return lhs.message_id < rhs.message_id;
            });

  std::priority_queue<LikeResult, std::vector<LikeResult>, LikeResultComparer>
      pq;
  for (size_t i = 0; i < messages.size(); ++i) {
    if (i > 0 && messages[i].person_id == messages[i - 1].person_id) {
      continue;
    }
    if (pq.size() < kTopN) {
      pq.push(messages[i]);
      continue;
    }
    if (LikeResultComparer{}(messages[i], pq.top())) {
      pq.pop();
      pq.push(messages[i]);
    }
  }

  const auto post_has_creator_out = graph.GetGenericOutgoingGraphView(
      post_label, person_label, has_creator_label);
  const auto comment_has_creator_out = graph.GetGenericOutgoingGraphView(
      comment_label, person_label, has_creator_label);
  const bool post_has_creator_date = schema.edge_has_property(
      post_label, person_label, has_creator_label, "creationDate");
  const bool comment_has_creator_date = schema.edge_has_property(
      comment_label, person_label, has_creator_label, "creationDate");
  EdgeDataAccessor post_creator_accessor;
  EdgeDataAccessor comment_creator_accessor;
  if (post_has_creator_date) {
    post_creator_accessor = graph.GetEdgeDataAccessor(
        post_label, person_label, has_creator_label, "creationDate");
  }
  if (comment_has_creator_date) {
    comment_creator_accessor = graph.GetEdgeDataAccessor(
        comment_label, person_label, has_creator_label, "creationDate");
  }

  std::vector<LikeResult> results;
  results.reserve(pq.size());
  while (!pq.empty()) {
    results.push_back(pq.top());
    pq.pop();
  }

  execution::ValueColumnBuilder<int64_t> person_id_builder;
  execution::ValueColumnBuilder<std::string> first_name_builder;
  execution::ValueColumnBuilder<std::string> last_name_builder;
  execution::ValueColumnBuilder<DateTime> like_date_builder;
  execution::ValueColumnBuilder<int64_t> message_id_builder;
  execution::ValueColumnBuilder<std::string> message_content_builder;
  execution::ValueColumnBuilder<int32_t> minutes_latency_builder;
  execution::ValueColumnBuilder<bool> is_new_builder;

  for (size_t i = results.size(); i > 0; --i) {
    const auto& row = results[i - 1];
    person_id_builder.push_back_opt(row.person_id);
    first_name_builder.push_back_opt(
        std::string(first_name_col->get_view(row.person_vid)));
    last_name_builder.push_back_opt(
        std::string(last_name_col->get_view(row.person_vid)));
    like_date_builder.push_back_opt(DateTime(row.like_date_ms));
    message_id_builder.push_back_opt(row.message_id);
    message_content_builder.push_back_opt(ldbc::message_content(
        graph, post_label, comment_label, row.message_vid, row.is_post));

    int64_t message_creation_ms = 0;
    if (row.is_post) {
      if (post_has_creator_date) {
        const auto edges = post_has_creator_out.get_edges(row.message_vid);
        for (auto it = edges.begin(); it != edges.end(); ++it) {
          message_creation_ms =
              post_creator_accessor.get_typed_data<DateTime>(it).milli_second;
          break;
        }
      } else if (post_creation_date_col) {
        message_creation_ms =
            post_creation_date_col->get_view(row.message_vid).milli_second;
      }
    } else {
      if (comment_has_creator_date) {
        const auto edges = comment_has_creator_out.get_edges(row.message_vid);
        for (auto it = edges.begin(); it != edges.end(); ++it) {
          message_creation_ms =
              comment_creator_accessor.get_typed_data<DateTime>(it)
                  .milli_second;
          break;
        }
      } else if (comment_creation_date_col) {
        message_creation_ms =
            comment_creation_date_col->get_view(row.message_vid).milli_second;
      }
    }
    const int32_t minutes =
        static_cast<int32_t>((row.like_date_ms - message_creation_ms) /
                             kMillisPerMinute);
    minutes_latency_builder.push_back_opt(minutes);
    is_new_builder.push_back_opt(!friends[row.person_vid]);
  }

  std::array<std::shared_ptr<execution::IContextColumn>, kNumOutputColumns>
      output_columns;
  output_columns[0] = person_id_builder.finish();
  output_columns[1] = first_name_builder.finish();
  output_columns[2] = last_name_builder.finish();
  output_columns[3] = like_date_builder.finish();
  output_columns[4] = message_id_builder.finish();
  output_columns[5] = message_content_builder.finish();
  output_columns[6] = minutes_latency_builder.finish();
  output_columns[7] = is_new_builder.finish();

  return ldbc::make_output_context(ic7_input.output_aliases,
                                   {output_columns[0], output_columns[1],
                                    output_columns[2], output_columns[3],
                                    output_columns[4], output_columns[5],
                                    output_columns[6], output_columns[7]});
}

}  // namespace

function::function_set IC7Function::getFunctionSet() {
  auto function = std::make_unique<function::NeugCallFunction>(
      IC7Function::name, std::vector<common::DataTypeId>{common::DataTypeId::kInt64},
      std::vector<std::pair<std::string, common::DataTypeId>>{
          {"personId", common::DataTypeId::kInt64},
          {"personFirstName", common::DataTypeId::kVarchar},
          {"personLastName", common::DataTypeId::kVarchar},
          {"likeCreationDate", common::DataTypeId::kTimestampMs},
          {"messageId", common::DataTypeId::kInt64},
          {"messageContent", common::DataTypeId::kVarchar},
          {"minutesLatency", common::DataTypeId::kInt32},
          {"isNew", common::DataTypeId::kBoolean}});
  function->bindFunc = bind_ic7;
  function->execFunc = exec_ic7;
  function::function_set function_set;
  function_set.push_back(std::move(function));
  return function_set;
}

}  // namespace ldbc_ic
}  // namespace extension
}  // namespace neug
