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

#include "ic9.h"

#include <queue>
#include <vector>

#include "ldbc_common.h"
#include "neug/common/columns/value_columns.h"
#include "neug/execution/common/context_chunk.h"
#include "neug/utils/exception/exception.h"

namespace neug {
namespace extension {
namespace ldbc {
class IC9 {
 public:
  static constexpr size_t kTopN = 20;

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

  static void scan_messages(
      const ldbc::TypedView& has_creator_in,
      const StorageReadInterface::vertex_column_t<int64_t>& message_id_col,
      bool is_post, vid_t friend_vid, int64_t max_date_ms,
      const StorageReadInterface::vertex_column_t<DateTime>* message_date_col,
      std::priority_queue<MessageInfo, std::vector<MessageInfo>,
                          MessageInfoComparer>& pq) {
    ldbc::foreach_incoming_nbr_between(
        has_creator_in, friend_vid, 0, max_date_ms,
        [&](vid_t message_vid, const DateTime& creation_date) {
          MessageInfo info;
          info.message_vid = message_vid;
          info.person_vid = friend_vid;
          info.creation_date_ms = creation_date.milli_second;
          info.is_post = is_post;
          if (pq.size() < kTopN) {
            info.message_id = message_id_col.get_view(message_vid);
            pq.push(info);
            return;
          }
          const auto& worst = pq.top();
          if (info.creation_date_ms > worst.creation_date_ms) {
            pq.pop();
            info.message_id = message_id_col.get_view(message_vid);
            pq.push(info);
            return;
          }

          if (info.creation_date_ms == worst.creation_date_ms) {
            info.message_id = message_id_col.get_view(message_vid);
            if (info.message_id < worst.message_id) {
              pq.pop();
              pq.push(info);
            }
          }
        });
  }

  static std::unique_ptr<function::CallFuncInputBase> bind(
      const Schema& /*schema*/, const execution::ContextMeta& /*ctx_meta*/,
      const ::physical::PhysicalPlan& plan, int op_idx) {
    const auto& params =
        plan.plan(op_idx).opr().procedure_call().query().arguments();
    if (params.size() < 2) {
      THROW_INVALID_ARGUMENT_EXCEPTION(
          "ic9 requires 2 arguments: personId and maxDate");
    }
    auto input = std::make_unique<IC9FuncInput>();
    ldbc::bind_ldbc_call(plan, op_idx, input.get());
    return input;
  }

  static execution::Context exec(const function::CallFuncInputBase& input,
                                 IStorageInterface& graph_iface) {
    const auto& args = dynamic_cast<const IC9FuncInput&>(input);
    const int64_t person_id = args.person_id;
    const int64_t max_date_ms = args.max_date_ms;
    const auto& graph = dynamic_cast<const StorageReadInterface&>(graph_iface);
    const auto& schema = graph.schema();

    const label_t person_label = schema.get_vertex_label_id("PERSON");
    const label_t post_label = schema.get_vertex_label_id("POST");
    const label_t comment_label = schema.get_vertex_label_id("COMMENT");
    const label_t knows_label = schema.get_edge_label_id("KNOWS");
    const label_t has_creator_label = schema.get_edge_label_id("HASCREATOR");

    auto first_name_col = ldbc::get_vertex_column<std::string_view>(
        graph, person_label, "firstName");
    auto last_name_col = ldbc::get_vertex_column<std::string_view>(
        graph, person_label, "lastName");
    auto post_date_col =
        ldbc::get_vertex_column<DateTime>(graph, post_label, "creationDate");
    auto comment_date_col =
        ldbc::get_vertex_column<DateTime>(graph, comment_label, "creationDate");
    auto person_id_col =
        ldbc::get_vertex_column<int64_t>(graph, person_label, "id");
    auto post_id_col =
        ldbc::get_vertex_column<int64_t>(graph, post_label, "id");
    auto comment_id_col =
        ldbc::get_vertex_column<int64_t>(graph, comment_label, "id");
    if (!first_name_col || !last_name_col) {
      THROW_RUNTIME_ERROR("ic9: failed to load required LDBC property columns");
    }

    vid_t root = StorageReadInterface::kInvalidVid;
    if (!graph.GetVertexIndex(person_label, Value::INT64(person_id), root)) {
      return execution::Context{};
    }

    const auto post_has_creator_in = ldbc::get_typed_incoming_view(
        graph, person_label, post_label, has_creator_label);
    const auto comment_has_creator_in = ldbc::get_typed_incoming_view(
        graph, person_label, comment_label, has_creator_label);

    std::priority_queue<MessageInfo, std::vector<MessageInfo>,
                        MessageInfoComparer>
        pq;
    ldbc::foreach_knows_1d_2d_neighbor(
        graph, person_label, knows_label, root, [&](vid_t friend_vid) {
          scan_messages(post_has_creator_in, *post_id_col, true, friend_vid,
                        max_date_ms, post_date_col.get(), pq);
          scan_messages(comment_has_creator_in, *comment_id_col, false,
                        friend_vid, max_date_ms, comment_date_col.get(), pq);
        });

    std::vector<MessageInfo> results;
    results.reserve(pq.size());
    while (!pq.empty()) {
      results.push_back(pq.top());
      pq.pop();
    }

    ValueColumnBuilder<int64_t> person_id_builder;
    ValueColumnBuilder<std::string> first_name_builder;
    ValueColumnBuilder<std::string> last_name_builder;
    ValueColumnBuilder<int64_t> message_id_builder;
    ValueColumnBuilder<std::string> message_content_builder;
    ValueColumnBuilder<DateTime> message_date_builder;

    for (size_t i = results.size(); i > 0; --i) {
      const auto& row = results[i - 1];
      person_id_builder.push_back_opt(person_id_col->get_view(row.person_vid));
      first_name_builder.push_back_opt(
          std::string(first_name_col->get_view(row.person_vid)));
      last_name_builder.push_back_opt(
          std::string(last_name_col->get_view(row.person_vid)));
      message_id_builder.push_back_opt(row.message_id);
      message_content_builder.push_back_opt(ldbc::message_content(
          graph, post_label, comment_label, row.message_vid, row.is_post));
      message_date_builder.push_back_opt(DateTime(row.creation_date_ms));
    }

    execution::ContextChunk chunk;
    chunk.set(0, person_id_builder.finish());
    chunk.set(1, first_name_builder.finish());
    chunk.set(2, last_name_builder.finish());
    chunk.set(3, message_id_builder.finish());
    chunk.set(4, message_content_builder.finish());
    chunk.set(5, message_date_builder.finish());
    execution::Context ctx;
    ctx.append_chunk(std::move(chunk));
    ctx.tag_ids = args.output_aliases;
    return ctx;
  }
};

function::function_set IC9Function::getFunctionSet() {
  auto fn = std::make_unique<function::NeugCallFunction>(
      IC9Function::name,
      function::call_input_types{common::DataType(common::DataTypeId::kInt64),
                                 common::DataType(common::DataTypeId::kInt64)},
      function::call_output_columns{
          {"personId", common::DataType(common::DataTypeId::kInt64)},
          {"personFirstName", common::DataType(common::DataTypeId::kVarchar)},
          {"personLastName", common::DataType(common::DataTypeId::kVarchar)},
          {"messageId", common::DataType(common::DataTypeId::kInt64)},
          {"messageContent", common::DataType(common::DataTypeId::kVarchar)},
          {"messageCreationDate",
           common::DataType(common::DataTypeId::kTimestampMs)}});
  fn->bindFunc = IC9::bind;
  fn->execFunc = IC9::exec;
  function::function_set set;
  set.push_back(std::move(fn));
  return set;
}

}  // namespace ldbc
}  // namespace extension
}  // namespace neug
