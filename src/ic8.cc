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

#include "ic8.h"

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
constexpr size_t kNumOutputColumns = 6;

struct CommentResult {
  vid_t comment_vid = 0;
  vid_t author_vid = 0;
  int64_t creation_date_ms = 0;
  int64_t comment_id = 0;
};

struct CommentResultComparer {
  bool operator()(const CommentResult& lhs, const CommentResult& rhs) const {
    if (lhs.creation_date_ms > rhs.creation_date_ms) {
      return true;
    }
    if (lhs.creation_date_ms < rhs.creation_date_ms) {
      return false;
    }
    return lhs.comment_id > rhs.comment_id;
  }
};

std::unique_ptr<function::CallFuncInputBase> bind_ic8(
    const Schema& /*schema*/, const execution::ContextMeta& /*ctx_meta*/,
    const ::physical::PhysicalPlan& plan, int op_idx) {
  const auto& params =
      plan.plan(op_idx).opr().procedure_call().query().arguments();
  auto input = std::make_unique<IC8FuncInput>();
  ldbc::bind_ldbc_call(plan, op_idx, input.get());
  return input;
}

void consider_comment(
    std::priority_queue<CommentResult, std::vector<CommentResult>,
                        CommentResultComparer>& pq,
    const CommentResult& candidate) {
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
      candidate.comment_id < worst.comment_id) {
    pq.pop();
    pq.push(candidate);
  }
}

void scan_replies(
    const StorageReadInterface& graph, label_t comment_label,
    label_t person_label, label_t has_creator_label,
    const CsrView& reply_in_view, const CsrView& comment_has_creator_out,
    vid_t message_vid, bool has_creator_date,
    const EdgeDataAccessor& creator_accessor,
    const StorageReadInterface::vertex_column_t<DateTime>* comment_date_col,
    std::priority_queue<CommentResult, std::vector<CommentResult>,
                        CommentResultComparer>& pq) {
  const auto replies = reply_in_view.get_edges(message_vid);
  for (auto it = replies.begin(); it != replies.end(); ++it) {
    const vid_t comment_vid = *it;
    const vid_t author_vid =
        ldbc::get_single_out_neighbor(comment_has_creator_out, comment_vid);
    if (author_vid == StorageReadInterface::kInvalidVid) {
      continue;
    }
    CommentResult info;
    info.comment_vid = comment_vid;
    info.author_vid = author_vid;
    info.comment_id =
        graph.GetVertexId(comment_label, comment_vid).GetValue<int64_t>();
    if (has_creator_date) {
      const auto authors = comment_has_creator_out.get_edges(comment_vid);
      for (auto author_it = authors.begin(); author_it != authors.end();
           ++author_it) {
        info.creation_date_ms =
            creator_accessor.get_typed_data<DateTime>(author_it).milli_second;
        break;
      }
    } else if (comment_date_col) {
      info.creation_date_ms =
          comment_date_col->get_view(comment_vid).milli_second;
    }
    consider_comment(pq, info);
  }
}

execution::Context exec_ic8(const function::CallFuncInputBase& input,
                            IStorageInterface& graph_iface,
                            const execution::ParamsMap& params) {
  const auto& ic8_input = dynamic_cast<const IC8FuncInput&>(input);
  const int64_t person_id = params.at("personId").GetValue<int64_t>();
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
  auto comment_content_col = ldbc::get_vertex_column<std::string_view>(
      graph, comment_label, "content");
  auto comment_date_col =
      ldbc::get_vertex_column<DateTime>(graph, comment_label, "creationDate");
  if (!first_name_col || !last_name_col || !comment_content_col) {
    THROW_RUNTIME_ERROR("ic8: failed to load required LDBC property columns");
  }

  vid_t root = StorageReadInterface::kInvalidVid;
  if (!graph.GetVertexIndex(person_label, execution::Value::INT64(person_id),
                            root)) {
    return execution::Context{};
  }

  const auto post_has_creator_in = graph.GetGenericIncomingGraphView(
      person_label, post_label, has_creator_label);
  const auto comment_has_creator_in = graph.GetGenericIncomingGraphView(
      person_label, comment_label, has_creator_label);
  const auto comment_reply_of_post_in = graph.GetGenericIncomingGraphView(
      post_label, comment_label, reply_of_label);
  const auto comment_reply_of_comment_in = graph.GetGenericIncomingGraphView(
      comment_label, comment_label, reply_of_label);
  const bool has_creator_date = schema.edge_has_property(
      comment_label, person_label, has_creator_label, "creationDate");
  EdgeDataAccessor creator_accessor;
  if (has_creator_date) {
    creator_accessor = graph.GetEdgeDataAccessor(
        comment_label, person_label, has_creator_label, "creationDate");
  }
  const auto comment_has_creator_out = graph.GetGenericOutgoingGraphView(
      comment_label, person_label, has_creator_label);

  std::priority_queue<CommentResult, std::vector<CommentResult>,
                      CommentResultComparer>
      pq;
  const auto root_posts = post_has_creator_in.get_edges(root);
  for (auto it = root_posts.begin(); it != root_posts.end(); ++it) {
    scan_replies(graph, comment_label, person_label, has_creator_label,
                 comment_reply_of_post_in, comment_has_creator_out, *it,
                 has_creator_date, creator_accessor, comment_date_col.get(),
                 pq);
  }
  const auto root_comments = comment_has_creator_in.get_edges(root);
  for (auto it = root_comments.begin(); it != root_comments.end(); ++it) {
    scan_replies(graph, comment_label, person_label, has_creator_label,
                 comment_reply_of_comment_in, comment_has_creator_out, *it,
                 has_creator_date, creator_accessor, comment_date_col.get(),
                 pq);
  }

  std::vector<CommentResult> results;
  results.reserve(pq.size());
  while (!pq.empty()) {
    results.push_back(pq.top());
    pq.pop();
  }

  execution::ValueColumnBuilder<int64_t> person_id_builder;
  execution::ValueColumnBuilder<std::string> first_name_builder;
  execution::ValueColumnBuilder<std::string> last_name_builder;
  execution::ValueColumnBuilder<DateTime> comment_date_builder;
  execution::ValueColumnBuilder<int64_t> comment_id_builder;
  execution::ValueColumnBuilder<std::string> comment_content_builder;

  for (size_t i = results.size(); i > 0; --i) {
    const auto& row = results[i - 1];
    person_id_builder.push_back_opt(
        graph.GetVertexId(person_label, row.author_vid).GetValue<int64_t>());
    first_name_builder.push_back_opt(
        std::string(first_name_col->get_view(row.author_vid)));
    last_name_builder.push_back_opt(
        std::string(last_name_col->get_view(row.author_vid)));
    comment_date_builder.push_back_opt(DateTime(row.creation_date_ms));
    comment_id_builder.push_back_opt(row.comment_id);
    comment_content_builder.push_back_opt(
        std::string(comment_content_col->get_view(row.comment_vid)));
  }

  std::array<std::shared_ptr<execution::IContextColumn>, kNumOutputColumns>
      output_columns;
  output_columns[0] = person_id_builder.finish();
  output_columns[1] = first_name_builder.finish();
  output_columns[2] = last_name_builder.finish();
  output_columns[3] = comment_date_builder.finish();
  output_columns[4] = comment_id_builder.finish();
  output_columns[5] = comment_content_builder.finish();

  return ldbc::make_output_context(
      ic8_input.output_aliases,
      {output_columns[0], output_columns[1], output_columns[2],
       output_columns[3], output_columns[4], output_columns[5]});
}

}  // namespace

function::function_set IC8Function::getFunctionSet() {
  auto function = std::make_unique<function::NeugCallFunction>(
      IC8Function::name,
      std::vector<common::DataTypeId>{common::DataTypeId::kInt64},
      function::call_output_columns{
          {"personId", common::DataTypeId::kInt64},
          {"personFirstName", common::DataTypeId::kVarchar},
          {"personLastName", common::DataTypeId::kVarchar},
          {"commentCreationDate", common::DataTypeId::kTimestampMs},
          {"commentId", common::DataTypeId::kInt64},
          {"commentContent", common::DataTypeId::kVarchar}});
  function->bindFunc = bind_ic8;
  function->execFunc = exec_ic8;
  function::function_set function_set;
  function_set.push_back(std::move(function));
  return function_set;
}

}  // namespace ldbc_ic
}  // namespace extension
}  // namespace neug
