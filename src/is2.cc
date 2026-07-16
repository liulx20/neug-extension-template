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

#include "is2.h"

#include <array>
#include <queue>
#include <vector>

#include "ldbc_common.h"
#include "neug/execution/common/context_chunk.h"
#include "neug/common/columns/value_columns.h"
#include "neug/utils/exception/exception.h"

namespace neug {
namespace extension {
namespace ldbc_ic {
class IS2 {
public:
static constexpr size_t kTopN = 10;

struct MessageInfo {
  vid_t message_vid = 0;
  int64_t message_id = 0;
  int64_t creation_date_ms = 0;
  bool is_post = false;
};

struct MessageInfoComparer {
  bool operator()(const MessageInfo& lhs, const MessageInfo& rhs) const {
    if (lhs.creation_date_ms > rhs.creation_date_ms) {
      return true;
    }
    if (lhs.creation_date_ms < rhs.creation_date_ms) {
      return false;
    }
    return lhs.message_id > rhs.message_id;
  }
};

static void consider_message(std::priority_queue<MessageInfo, std::vector<MessageInfo>,
                                          MessageInfoComparer>& pq,
                      const MessageInfo& candidate) {
  if (pq.size() < kTopN) {
    pq.push(candidate);
    return;
  }
  const auto& worst = pq.top();
  if (candidate.creation_date_ms > worst.creation_date_ms) {
    pq.pop();
    pq.push(candidate);
    return;
  }
  if (candidate.creation_date_ms == worst.creation_date_ms &&
      candidate.message_id < worst.message_id) {
    pq.pop();
    pq.push(candidate);
  }
}

static void scan_person_messages(
    const StorageReadInterface& graph, const Schema& schema,
    const ldbc::TypedView& has_creator_in, label_t person_label,
    label_t message_label, bool is_post, vid_t person_vid,
    const StorageReadInterface::vertex_column_t<int64_t>& message_id_col,
    std::priority_queue<MessageInfo, std::vector<MessageInfo>,
                        MessageInfoComparer>& pq) {
  const label_t has_creator_label = schema.get_edge_label_id("HASCREATOR");
  const bool has_edge_date = schema.edge_has_property(
      message_label, person_label, has_creator_label, "creationDate");
  auto vertex_date_col =
      ldbc::get_vertex_column<DateTime>(graph, message_label, "creationDate");

  if (has_edge_date) {
    ldbc::foreach_incoming_nbr_gt(
        has_creator_in, person_vid, DateTime(-1),
        [&](vid_t message_vid, const DateTime& creation_date) {
          MessageInfo info;
          info.message_vid = message_vid;
          info.message_id = message_id_col.get_view(message_vid);
          info.creation_date_ms = creation_date.milli_second;
          info.is_post = is_post;
          consider_message(pq, info);
        });
    return;
  }

  const auto in_view = graph.GetGenericIncomingGraphView(
      person_label, message_label, has_creator_label);
  for (auto it = in_view.get_edges(person_vid).begin();
       it != in_view.get_edges(person_vid).end(); ++it) {
    const vid_t message_vid = *it;
    int64_t creation_date_ms = 0;
    if (vertex_date_col) {
      creation_date_ms = vertex_date_col->get_view(message_vid).milli_second;
    }
    MessageInfo info;
    info.message_vid = message_vid;
    info.message_id = message_id_col.get_view(message_vid);
    info.creation_date_ms = creation_date_ms;
    info.is_post = is_post;
    consider_message(pq, info);
  }
}

static std::unique_ptr<function::CallFuncInputBase> bind(
    const Schema& /*schema*/, const execution::ContextMeta& /*ctx_meta*/,
    const ::physical::PhysicalPlan& plan, int op_idx) {
  const auto& params =
      plan.plan(op_idx).opr().procedure_call().query().arguments();
  auto input = std::make_unique<IS2FuncInput>();
  ldbc::bind_ldbc_call(plan, op_idx, input.get());
  return input;
}

static execution::Context exec(const function::CallFuncInputBase& input,
                            IStorageInterface& graph_iface) {
  const auto& is2_input = dynamic_cast<const IS2FuncInput&>(input);
  const int64_t person_id = is2_input.person_id;
  const auto& graph = dynamic_cast<const StorageReadInterface&>(graph_iface);
  const auto& schema = graph.schema();

  const label_t person_label = schema.get_vertex_label_id("PERSON");
  const label_t post_label = schema.get_vertex_label_id("POST");
  const label_t comment_label = schema.get_vertex_label_id("COMMENT");
  const label_t has_creator_label = schema.get_edge_label_id("HASCREATOR");
  const label_t reply_of_label = schema.get_edge_label_id("REPLYOF");

  auto first_name_col = ldbc::get_vertex_column<std::string_view>(
      graph, person_label, "firstName");
  auto last_name_col = ldbc::get_vertex_column<std::string_view>(
      graph, person_label, "lastName");
  auto person_id_col = ldbc::get_vertex_column<int64_t>(graph, person_label, "id");
  auto post_id_col = ldbc::get_vertex_column<int64_t>(graph, post_label, "id");
  auto comment_id_col = ldbc::get_vertex_column<int64_t>(graph, comment_label, "id");
  if (!first_name_col || !last_name_col) {
    THROW_RUNTIME_ERROR("is2: failed to load required LDBC property columns");
  }

  vid_t person_vid = StorageReadInterface::kInvalidVid;
  if (!graph.GetVertexIndex(person_label, Value::INT64(person_id),
                            person_vid)) {
    return execution::Context{};
  }

  const auto post_has_creator_in = ldbc::get_typed_incoming_view(
      graph, person_label, post_label, has_creator_label);
  const auto comment_has_creator_in = ldbc::get_typed_incoming_view(
      graph, person_label, comment_label, has_creator_label);

  std::priority_queue<MessageInfo, std::vector<MessageInfo>,
                      MessageInfoComparer>
      pq;
  scan_person_messages(graph, schema, post_has_creator_in, person_label,
                       post_label, true, person_vid, *post_id_col, pq);
  scan_person_messages(graph, schema, comment_has_creator_in, person_label,
                       comment_label, false, person_vid, *comment_id_col, pq);

  std::vector<MessageInfo> results;
  results.reserve(pq.size());
  while (!pq.empty()) {
    results.push_back(pq.top());
    pq.pop();
  }

  const auto post_has_creator_out = graph.GetGenericOutgoingGraphView(
      post_label, person_label, has_creator_label);

  ValueColumnBuilder<int64_t> message_id_builder;
  ValueColumnBuilder<std::string> message_content_builder;
  ValueColumnBuilder<DateTime> message_date_builder;
  ValueColumnBuilder<int64_t> original_post_id_builder;
  ValueColumnBuilder<int64_t> original_post_author_id_builder;
  ValueColumnBuilder<std::string> original_post_author_first_builder;
  ValueColumnBuilder<std::string> original_post_author_last_builder;

  message_id_builder.reserve(results.size());
  message_content_builder.reserve(results.size());
  message_date_builder.reserve(results.size());
  original_post_id_builder.reserve(results.size());
  original_post_author_id_builder.reserve(results.size());
  original_post_author_first_builder.reserve(results.size());
  original_post_author_last_builder.reserve(results.size());

  for (size_t i = results.size(); i > 0; --i) {
    const auto& row = results[i - 1];
    message_id_builder.push_back_opt(row.message_id);
    message_content_builder.push_back_opt(ldbc::message_content(
        graph, post_label, comment_label, row.message_vid, row.is_post));
    message_date_builder.push_back_opt(DateTime(row.creation_date_ms));

    if (row.is_post) {
      original_post_id_builder.push_back_opt(row.message_id);
      original_post_author_id_builder.push_back_opt(person_id);
      original_post_author_first_builder.push_back_opt(
          std::string(first_name_col->get_view(person_vid)));
      original_post_author_last_builder.push_back_opt(
          std::string(last_name_col->get_view(person_vid)));
    } else {
      const vid_t post_vid =
          ldbc::resolve_root_post(graph, post_label, comment_label,
                                  reply_of_label, row.message_vid, false);
      const int64_t post_id =
          post_id_col->get_view(post_vid);
      original_post_id_builder.push_back_opt(post_id);
      const vid_t author_vid =
          ldbc::get_single_out_neighbor(post_has_creator_out, post_vid);
      original_post_author_id_builder.push_back_opt(
          person_id_col->get_view(author_vid));
      original_post_author_first_builder.push_back_opt(
          std::string(first_name_col->get_view(author_vid)));
      original_post_author_last_builder.push_back_opt(
          std::string(last_name_col->get_view(author_vid)));
    }
  }


  execution::ContextChunk chunk;
  chunk.set(0, message_id_builder.finish());
  chunk.set(1, message_content_builder.finish());
  chunk.set(2, message_date_builder.finish());
  chunk.set(3, original_post_id_builder.finish());
  chunk.set(4, original_post_author_id_builder.finish());
  chunk.set(5, original_post_author_first_builder.finish());
  chunk.set(6, original_post_author_last_builder.finish());
  execution::Context ctx;
  ctx.append_chunk(std::move(chunk));
  ctx.tag_ids = is2_input.output_aliases;
  return ctx;
}

};

function::function_set IS2Function::getFunctionSet() {
  auto function = std::make_unique<function::NeugCallFunction>(
      IS2Function::name,
      function::call_input_types{common::DataType(common::DataTypeId::kInt64)},
      function::call_output_columns{
          {"messageId", common::DataType(common::DataTypeId::kInt64)},
          {"messageContent", common::DataType(common::DataTypeId::kVarchar)},
          {"messageCreationDate", common::DataType(common::DataTypeId::kTimestampMs)},
          {"originalPostId", common::DataType(common::DataTypeId::kInt64)},
          {"originalPostAuthorId", common::DataType(common::DataTypeId::kInt64)},
          {"originalPostAuthorFirstName", common::DataType(common::DataTypeId::kVarchar)},
          {"originalPostAuthorLastName", common::DataType(common::DataTypeId::kVarchar)}});
  function->bindFunc = IS2::bind;
  function->execFunc = IS2::exec;
  function::function_set function_set;
  function_set.push_back(std::move(function));
  return function_set;
}

}  // namespace ldbc_ic
}  // namespace extension
}  // namespace neug
