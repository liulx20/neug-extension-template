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
#include <queue>
#include <vector>

#include "ldbc_common.h"
#include "neug/common/columns/value_columns.h"
#include "neug/execution/common/context_chunk.h"
#include "neug/utils/exception/exception.h"

namespace neug {
namespace extension {
namespace ldbc {
class IC7 {
 public:
  static constexpr size_t kTopN = 20;
  static constexpr int64_t kMillisPerMinute = 60 * 1000L;

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

  static std::unique_ptr<function::CallFuncInputBase> bind(
      const Schema& /*schema*/, const execution::ContextMeta& /*ctx_meta*/,
      const ::physical::PhysicalPlan& plan, int op_idx) {
    auto input = std::make_unique<IC7FuncInput>();
    ldbc::bind_ldbc_call(plan, op_idx, *input);
    return input;
  }

  static void collect_likes(
      const StorageReadInterface& graph, const Schema& schema,
      label_t person_label, label_t message_label, label_t likes_label,
      label_t has_creator_label, bool is_post, vid_t root,
      const StorageReadInterface::vertex_column_t<DateTime>* message_date_col,
      const StorageReadInterface::vertex_column_t<int64_t>& message_id_col,
      const StorageReadInterface::vertex_column_t<int64_t>& person_id_col,
      std::vector<LikeResult>& messages) {
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
      const int64_t message_id = message_id_col.get_view(message_vid);
      const auto likes = person_likes_message_in.get_edges(message_vid);
      for (auto like_it = likes.begin(); like_it != likes.end(); ++like_it) {
        LikeResult info;
        info.person_vid = *like_it;
        info.message_vid = message_vid;
        info.person_id = person_id_col.get_view(info.person_vid);
        info.message_id = message_id;
        info.is_post = is_post;
        if (has_like_date) {
          info.like_date_ms =
              like_accessor.get_typed_data<DateTime>(like_it).milli_second;
        } else if (message_date_col) {
          info.like_date_ms =
              message_date_col->get_view(message_vid).milli_second;
        }
        messages.push_back(info);
      }
    }
  }

  static execution::Context exec(const function::CallFuncInputBase& input,
                                 IStorageInterface& graph_iface) {
    const auto& ic7_input = dynamic_cast<const IC7FuncInput&>(input);
    const int64_t person_id = ic7_input.person_id;
    const auto& graph = dynamic_cast<const StorageReadInterface&>(graph_iface);
    const auto& schema = graph.schema();

    const label_t person_label = schema.get_vertex_label_id("PERSON");
    const label_t post_label = schema.get_vertex_label_id("POST");
    const label_t comment_label = schema.get_vertex_label_id("COMMENT");
    const label_t knows_label = schema.get_edge_label_id("KNOWS");
    const label_t likes_label = schema.get_edge_label_id("LIKES");
    const label_t has_creator_label = schema.get_edge_label_id("HASCREATOR");

    auto first_name_col = ldbc::get_vertex_column<std::string_view>(
        graph, person_label, "firstName");
    auto last_name_col = ldbc::get_vertex_column<std::string_view>(
        graph, person_label, "lastName");
    auto post_creation_date_col =
        ldbc::get_vertex_column<DateTime>(graph, post_label, "creationDate");
    auto comment_creation_date_col =
        ldbc::get_vertex_column<DateTime>(graph, comment_label, "creationDate");
    auto person_id_col =
        ldbc::get_vertex_column<int64_t>(graph, person_label, "id");
    auto post_id_col =
        ldbc::get_vertex_column<int64_t>(graph, post_label, "id");
    auto comment_id_col =
        ldbc::get_vertex_column<int64_t>(graph, comment_label, "id");

    vid_t root = StorageReadInterface::kInvalidVid;
    if (!graph.GetVertexIndex(person_label, Value::INT64(person_id), root)) {
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
                  *post_id_col, *person_id_col, messages);
    collect_likes(graph, schema, person_label, comment_label, likes_label,
                  has_creator_label, false, root,
                  comment_creation_date_col.get(), *comment_id_col,
                  *person_id_col, messages);

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


    std::vector<LikeResult> results;
    results.reserve(pq.size());
    while (!pq.empty()) {
      results.push_back(pq.top());
      pq.pop();
    }

    ValueColumnBuilder<int64_t> person_id_builder;
    ValueColumnBuilder<std::string> first_name_builder;
    ValueColumnBuilder<std::string> last_name_builder;
    ValueColumnBuilder<DateTime> like_date_builder;
    ValueColumnBuilder<int64_t> message_id_builder;
    ValueColumnBuilder<std::string> message_content_builder;
    ValueColumnBuilder<int32_t> minutes_latency_builder;
    ValueColumnBuilder<bool> is_new_builder;

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
    
            message_creation_ms =
              post_creation_date_col->get_view(row.message_vid).milli_second;
      } else {

          message_creation_ms =
              comment_creation_date_col->get_view(row.message_vid).milli_second;
      }
      const int32_t minutes = static_cast<int32_t>(
          (row.like_date_ms - message_creation_ms) / kMillisPerMinute);
      minutes_latency_builder.push_back_opt(minutes);
      is_new_builder.push_back_opt(!friends[row.person_vid]);
    }

    execution::ContextChunk chunk;
    chunk.set(0, person_id_builder.finish());
    chunk.set(1, first_name_builder.finish());
    chunk.set(2, last_name_builder.finish());
    chunk.set(3, like_date_builder.finish());
    chunk.set(4, message_id_builder.finish());
    chunk.set(5, message_content_builder.finish());
    chunk.set(6, minutes_latency_builder.finish());
    chunk.set(7, is_new_builder.finish());
    execution::Context ctx;
    ctx.append_chunk(std::move(chunk));
    ctx.tag_ids = ic7_input.output_aliases;
    return ctx;
  }
};

function::function_set IC7Function::getFunctionSet() {
  auto function = std::make_unique<function::NeugCallFunction>(
      IC7Function::name,
      function::call_input_types{common::DataType(common::DataTypeId::kInt64)},
      function::call_output_columns{
          {"personId", common::DataType(common::DataTypeId::kInt64)},
          {"personFirstName", common::DataType(common::DataTypeId::kVarchar)},
          {"personLastName", common::DataType(common::DataTypeId::kVarchar)},
          {"likeCreationDate",
           common::DataType(common::DataTypeId::kTimestampMs)},
          {"messageId", common::DataType(common::DataTypeId::kInt64)},
          {"messageContent", common::DataType(common::DataTypeId::kVarchar)},
          {"minutesLatency", common::DataType(common::DataTypeId::kInt32)},
          {"isNew", common::DataType(common::DataTypeId::kBoolean)}});
  function->bindFunc = IC7::bind;
  function->execFunc = IC7::exec;
  function::function_set function_set;
  function_set.push_back(std::move(function));
  return function_set;
}

}  // namespace ldbc
}  // namespace extension
}  // namespace neug
