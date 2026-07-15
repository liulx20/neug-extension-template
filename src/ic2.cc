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

#include "ic2.h"

#include <glog/logging.h>
#include <array>
#include <queue>
#include <vector>

#include "ldbc_common.h"
#include "neug/compiler/common/types/types.h"
#include "neug/execution/common/columns/value_columns.h"
#include "neug/execution/common/context.h"
#include "neug/execution/common/context_chunk.h"
#include "neug/execution/common/types/value.h"
#include "neug/storages/csr/csr_view.h"
#include "neug/utils/exception/exception.h"

namespace neug {
namespace extension {
namespace ldbc_ic {
namespace {

constexpr size_t kTopN = 20;
constexpr size_t kNumOutputColumns = 6;

struct MessageInfo {
  vid_t message_vid = 0;
  vid_t person_vid = 0;
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
    return lhs.message_id < rhs.message_id;
  }
};

template <typename T>
std::shared_ptr<StorageReadInterface::vertex_column_t<T>> get_vertex_column(
    const StorageReadInterface& graph, label_t label,
    const std::string& prop_name) {
  auto col = graph.GetVertexPropColumn(label, prop_name);
  return std::dynamic_pointer_cast<StorageReadInterface::vertex_column_t<T>>(
      col);
}

void foreach_knows_neighbor(const StorageReadInterface& graph,
                            label_t person_label, label_t knows_label,
                            vid_t root, const auto& func) {
  const auto out_view = graph.GetGenericOutgoingGraphView(
      person_label, person_label, knows_label);
  const auto in_view = graph.GetGenericIncomingGraphView(
      person_label, person_label, knows_label);

  for (auto it = out_view.get_edges(root).begin();
       it != out_view.get_edges(root).end(); ++it) {
    func(*it);
  }
  for (auto it = in_view.get_edges(root).begin();
       it != in_view.get_edges(root).end(); ++it) {
    func(*it);
  }
}

void scan_messages_for_friend(
    const ldbc::DateTimeIncomingView& has_creator_in,
    const StorageReadInterface::vertex_column_t<int64_t>& message_id_col,
    bool is_post, vid_t friend_vid, int64_t& min_date_ms, int64_t max_date_ms,
    std::priority_queue<MessageInfo, std::vector<MessageInfo>,
                        MessageInfoComparer>& pq) {
  ldbc::foreach_incoming_nbr_lt(
      has_creator_in, friend_vid, DateTime(max_date_ms + 1),
      [&](vid_t message_vid, const DateTime& creation_date) {
        const int64_t creation_date_ms = creation_date.milli_second;
        if (creation_date_ms < min_date_ms) {
          return;
        }
        if (pq.size() < kTopN) {
          const int64_t message_id = message_id_col.get_view(message_vid);
          pq.push(MessageInfo{message_vid, friend_vid, message_id,
                              creation_date_ms, is_post});
          if (pq.size() == kTopN) {
            min_date_ms = pq.top().creation_date_ms;
          }
          return;
        }

        if (creation_date_ms > min_date_ms) {
          const int64_t message_id = message_id_col.get_view(message_vid);
          pq.pop();
          pq.push(MessageInfo{message_vid, friend_vid, message_id,
                              creation_date_ms, is_post});
          min_date_ms = pq.top().creation_date_ms;
          return;
        }

        if (creation_date_ms == min_date_ms) {
          const int64_t message_id = message_id_col.get_view(message_vid);
          if (message_id < pq.top().message_id) {
            pq.pop();
            pq.push(MessageInfo{message_vid, friend_vid, message_id,
                                creation_date_ms, is_post});
          }
        }
      });
}

std::unique_ptr<function::CallFuncInputBase> bind_ic2(
    const Schema& /*schema*/, const execution::ContextMeta& /*ctx_meta*/,
    const ::physical::PhysicalPlan& plan, int op_idx) {
  const auto& procedure_call = plan.plan(op_idx).opr().procedure_call();
  const auto& params = procedure_call.query().arguments();
  if (params.size() < 2) {
    THROW_INVALID_ARGUMENT_EXCEPTION(
        "ic2 requires 2 arguments: personId and maxDate");
  }

  auto input = std::make_unique<IC2FuncInput>();
  ldbc::bind_ldbc_call(plan, op_idx, input.get());
  return input;
}

execution::Context exec_ic2(const function::CallFuncInputBase& input,
                            IStorageInterface& graph_iface,
                            const execution::ParamsMap& params) {
  const auto& ic2_input = dynamic_cast<const IC2FuncInput&>(input);
  const int64_t person_id = params.at("personId").GetValue<int64_t>();
  const int64_t max_date_ms = params.at("maxDate").GetValue<int64_t>();
  const auto& graph = dynamic_cast<const StorageReadInterface&>(graph_iface);
  const auto& schema = graph.schema();

  const label_t person_label = schema.get_vertex_label_id("PERSON");
  const label_t post_label = schema.get_vertex_label_id("POST");
  const label_t comment_label = schema.get_vertex_label_id("COMMENT");
  const label_t knows_label = schema.get_edge_label_id("KNOWS");
  const label_t has_creator_label = schema.get_edge_label_id("HASCREATOR");

  auto first_name_col =
      get_vertex_column<std::string_view>(graph, person_label, "firstName");
  auto last_name_col =
      get_vertex_column<std::string_view>(graph, person_label, "lastName");
  auto post_content_col =
      get_vertex_column<std::string_view>(graph, post_label, "content");
  auto post_image_col =
      get_vertex_column<std::string_view>(graph, post_label, "imageFile");
  auto post_length_col =
      get_vertex_column<int32_t>(graph, post_label, "length");
  auto comment_content_col =
      get_vertex_column<std::string_view>(graph, comment_label, "content");
  auto person_id_col = ldbc::get_vertex_column<int64_t>(graph, person_label, "id");
  auto post_id_col = ldbc::get_vertex_column<int64_t>(graph, post_label, "id");
  auto comment_id_col = ldbc::get_vertex_column<int64_t>(graph, comment_label, "id");

  vid_t root = StorageReadInterface::kInvalidVid;
  if (!graph.GetVertexIndex(person_label, execution::Value::INT64(person_id),
                            root)) {
    return execution::Context{};
  }

  const auto post_has_creator_in = ldbc::get_typed_incoming_view(
      graph, person_label, post_label, has_creator_label);
  const auto comment_has_creator_in = ldbc::get_typed_incoming_view(
      graph, person_label, comment_label, has_creator_label);

  std::priority_queue<MessageInfo, std::vector<MessageInfo>,
                      MessageInfoComparer>
      pq;
  int64_t min_date_ms = 0;
  foreach_knows_neighbor(
      graph, person_label, knows_label, root, [&](vid_t friend_vid) {
        scan_messages_for_friend(post_has_creator_in, *post_id_col, true,
                                 friend_vid, min_date_ms, max_date_ms, pq);
        scan_messages_for_friend(comment_has_creator_in, *comment_id_col, false,
                                 friend_vid, min_date_ms, max_date_ms, pq);
      });

  std::vector<MessageInfo> results;
  results.reserve(pq.size());
  while (!pq.empty()) {
    results.push_back(pq.top());
    pq.pop();
  }

  execution::ValueColumnBuilder<int64_t> person_id_builder;
  execution::ValueColumnBuilder<std::string> first_name_builder;
  execution::ValueColumnBuilder<std::string> last_name_builder;
  execution::ValueColumnBuilder<int64_t> message_id_builder;
  execution::ValueColumnBuilder<std::string> message_content_builder;
  execution::ValueColumnBuilder<DateTime> message_date_builder;

  person_id_builder.reserve(results.size());
  first_name_builder.reserve(results.size());
  last_name_builder.reserve(results.size());
  message_id_builder.reserve(results.size());
  message_content_builder.reserve(results.size());
  message_date_builder.reserve(results.size());

  for (size_t i = results.size(); i > 0; --i) {
    const auto& row = results[i - 1];
    person_id_builder.push_back_opt(
        person_id_col->get_view(row.person_vid));
    first_name_builder.push_back_opt(
        std::string(first_name_col->get_view(row.person_vid)));
    last_name_builder.push_back_opt(
        std::string(last_name_col->get_view(row.person_vid)));
    message_id_builder.push_back_opt(row.message_id);

    if (row.is_post) {
      const auto& content = post_length_col->get_view(row.message_vid) == 0
                                ? post_image_col->get_view(row.message_vid)
                                : post_content_col->get_view(row.message_vid);
      message_content_builder.push_back_opt(std::string(content));
    } else {
      message_content_builder.push_back_opt(
          std::string(comment_content_col->get_view(row.message_vid)));
    }
    message_date_builder.push_back_opt(DateTime(row.creation_date_ms));
  }

  std::array<std::shared_ptr<execution::IContextColumn>, kNumOutputColumns>
      output_columns;
  output_columns[0] = person_id_builder.finish();
  output_columns[1] = first_name_builder.finish();
  output_columns[2] = last_name_builder.finish();
  output_columns[3] = message_id_builder.finish();
  output_columns[4] = message_content_builder.finish();
  output_columns[5] = message_date_builder.finish();

  execution::Context ctx;
  execution::ContextChunk out_chunk;
  ctx.tag_ids = ic2_input.output_aliases;
  for (size_t i = 0; i < ic2_input.output_aliases.size(); ++i) {
    out_chunk.set(ic2_input.output_aliases[i], output_columns[i]);
  }
  ctx.append_chunk(std::move(out_chunk));
  return ctx;
}

}  // namespace

function::function_set IC2Function::getFunctionSet() {
  auto function = std::make_unique<function::NeugCallFunction>(
      IC2Function::name,
      std::vector<common::DataTypeId>{common::DataTypeId::kInt64,
                                      common::DataTypeId::kInt64},
      function::call_output_columns{
          {"personId", common::DataType(common::DataTypeId::kInt64)},
          {"personFirstName", common::DataType(common::DataTypeId::kVarchar)},
          {"personLastName", common::DataType(common::DataTypeId::kVarchar)},
          {"messageId", common::DataType(common::DataTypeId::kInt64)},
          {"messageContent", common::DataType(common::DataTypeId::kVarchar)},
          {"messageCreationDate", common::DataType(common::DataTypeId::kTimestampMs)}});

  function->bindFunc = bind_ic2;
  function->execFunc = exec_ic2;

  function::function_set function_set;
  function_set.push_back(std::move(function));
  return function_set;
}

}  // namespace ldbc_ic
}  // namespace extension
}  // namespace neug
