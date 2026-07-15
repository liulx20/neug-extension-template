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

#include "is7.h"

#include <algorithm>
#include <array>
#include <vector>

#include "ldbc_common.h"
#include "neug/execution/common/columns/value_columns.h"
#include "neug/utils/exception/exception.h"

namespace neug {
namespace extension {
namespace ldbc_ic {
namespace {

constexpr size_t kNumOutputColumns = 7;

struct ReplyInfo {
  vid_t comment_vid = 0;
  vid_t author_vid = 0;
  int64_t comment_id = 0;
  int64_t author_id = 0;
  int64_t creation_date_ms = 0;
};

std::unique_ptr<function::CallFuncInputBase> bind_is7(
    const Schema& /*schema*/, const execution::ContextMeta& /*ctx_meta*/,
    const ::physical::PhysicalPlan& plan, int op_idx) {
  const auto& params =
      plan.plan(op_idx).opr().procedure_call().query().arguments();
  auto input = std::make_unique<IS7FuncInput>();
  ldbc::bind_ldbc_call(plan, op_idx, input.get());
  return input;
}

execution::Context exec_is7(const function::CallFuncInputBase& input,
                            IStorageInterface& graph_iface,
                            const execution::ParamsMap& params) {
  const auto& is7_input = dynamic_cast<const IS7FuncInput&>(input);
  const int64_t message_id = params.at("messageId").GetValue<int64_t>();
  const auto& graph = dynamic_cast<const StorageReadInterface&>(graph_iface);
  const auto& schema = graph.schema();

  const label_t person_label = schema.get_vertex_label_id("PERSON");
  const label_t post_label = schema.get_vertex_label_id("POST");
  const label_t comment_label = schema.get_vertex_label_id("COMMENT");
  const label_t reply_of_label = schema.get_edge_label_id("REPLYOF");
  const label_t has_creator_label = schema.get_edge_label_id("HASCREATOR");
  const label_t knows_label = schema.get_edge_label_id("KNOWS");

  auto first_name_col = ldbc::get_vertex_column<std::string_view>(
      graph, person_label, "firstName");
  auto last_name_col = ldbc::get_vertex_column<std::string_view>(
      graph, person_label, "lastName");
  auto comment_content_col = ldbc::get_vertex_column<std::string_view>(
      graph, comment_label, "content");
  auto person_id_col = ldbc::get_vertex_column<int64_t>(graph, person_label, "id");
  auto comment_id_col = ldbc::get_vertex_column<int64_t>(graph, comment_label, "id");

  vid_t message_vid = StorageReadInterface::kInvalidVid;
  bool is_post = false;
  if (!ldbc::find_message_vertex(graph, post_label, comment_label, message_id,
                                 &message_vid, &is_post)) {
    return execution::Context{};
  }

  const auto comment_has_creator_out = graph.GetGenericOutgoingGraphView(
      comment_label, person_label, has_creator_label);
  const bool has_edge_date = schema.edge_has_property(
      comment_label, person_label, has_creator_label, "creationDate");
  auto
    edge_accessor = graph.GetEdgeDataAccessor(
        comment_label, person_label, has_creator_label, "creationDate");
  

  const label_t message_label = is_post ? post_label : comment_label;
  const auto message_has_creator_out = graph.GetGenericOutgoingGraphView(
      message_label, person_label, has_creator_label);
  const vid_t message_author_vid =
      ldbc::get_single_out_neighbor(message_has_creator_out, message_vid);
  if (message_author_vid == StorageReadInterface::kInvalidVid) {
    return execution::Context{};
  }

  const CsrView& reply_in_view =
      is_post ? graph.GetGenericIncomingGraphView(post_label, comment_label,
                                                  reply_of_label)
              : graph.GetGenericIncomingGraphView(comment_label, comment_label,
                                                  reply_of_label);

  std::vector<ReplyInfo> replies;
  for (auto it = reply_in_view.get_edges(message_vid).begin();
       it != reply_in_view.get_edges(message_vid).end(); ++it) {
    const vid_t comment_vid = *it;
    const vid_t author_vid =
        ldbc::get_single_out_neighbor(comment_has_creator_out, comment_vid);
    if (author_vid == StorageReadInterface::kInvalidVid) {
      continue;
    }
    ReplyInfo info;
    info.comment_vid = comment_vid;
    info.author_vid = author_vid;
    info.comment_id = comment_id_col->get_view(comment_vid);
    info.author_id = person_id_col->get_view(author_vid);
      for (auto author_it =
               comment_has_creator_out.get_edges(comment_vid).begin();
           author_it != comment_has_creator_out.get_edges(comment_vid).end();
           ++author_it) {
        if (*author_it == author_vid) {
          info.creation_date_ms =
              edge_accessor.get_typed_data<DateTime>(author_it).milli_second;
          break;
        }
      }
   
    replies.push_back(info);
  }

  std::sort(replies.begin(), replies.end(),
            [](const ReplyInfo& lhs, const ReplyInfo& rhs) {
              if (lhs.creation_date_ms != rhs.creation_date_ms) {
                return lhs.creation_date_ms > rhs.creation_date_ms;
              }
              return lhs.author_id < rhs.author_id;
            });

  const auto knows_out = graph.GetGenericOutgoingGraphView(
      person_label, person_label, knows_label);
  const auto knows_in = graph.GetGenericIncomingGraphView(
      person_label, person_label, knows_label);
  std::vector<bool> knows_author(graph.GetVertexSet(person_label).size(),
                                 false);
  auto mark_knows = [&](const CsrView& view) {
    for (auto it = view.get_edges(message_author_vid).begin();
         it != view.get_edges(message_author_vid).end(); ++it) {
      knows_author[*it] = true;
    }
  };
  mark_knows(knows_out);
  mark_knows(knows_in);

  execution::ValueColumnBuilder<int64_t> comment_id_builder;
  execution::ValueColumnBuilder<std::string> comment_content_builder;
  execution::ValueColumnBuilder<DateTime> comment_date_builder;
  execution::ValueColumnBuilder<int64_t> reply_author_id_builder;
  execution::ValueColumnBuilder<std::string> reply_author_first_builder;
  execution::ValueColumnBuilder<std::string> reply_author_last_builder;
  execution::ValueColumnBuilder<bool> knows_builder;

  for (const auto& reply : replies) {
    comment_id_builder.push_back_opt(reply.comment_id);
    comment_content_builder.push_back_opt(
        std::string(comment_content_col->get_view(reply.comment_vid)));
    comment_date_builder.push_back_opt(DateTime(reply.creation_date_ms));
    reply_author_id_builder.push_back_opt(reply.author_id);
    reply_author_first_builder.push_back_opt(
        std::string(first_name_col->get_view(reply.author_vid)));
    reply_author_last_builder.push_back_opt(
        std::string(last_name_col->get_view(reply.author_vid)));
    knows_builder.push_back_opt(knows_author[reply.author_vid]);
  }

  std::array<std::shared_ptr<execution::IContextColumn>, kNumOutputColumns>
      output_columns;
  output_columns[0] = comment_id_builder.finish();
  output_columns[1] = comment_content_builder.finish();
  output_columns[2] = comment_date_builder.finish();
  output_columns[3] = reply_author_id_builder.finish();
  output_columns[4] = reply_author_first_builder.finish();
  output_columns[5] = reply_author_last_builder.finish();
  output_columns[6] = knows_builder.finish();

  return ldbc::make_output_context(
      is7_input.output_aliases,
      {output_columns[0], output_columns[1], output_columns[2],
       output_columns[3], output_columns[4], output_columns[5],
       output_columns[6]});
}

}  // namespace

function::function_set IS7Function::getFunctionSet() {
  auto function = std::make_unique<function::NeugCallFunction>(
      IS7Function::name,
      std::vector<common::DataTypeId>{common::DataTypeId::kInt64},
      function::call_output_columns{
          {"commentId", common::DataType(common::DataTypeId::kInt64)},
          {"commentContent", common::DataType(common::DataTypeId::kVarchar)},
          {"commentCreationDate", common::DataType(common::DataTypeId::kTimestampMs)},
          {"replyAuthorId", common::DataType(common::DataTypeId::kInt64)},
          {"replyAuthorFirstName", common::DataType(common::DataTypeId::kVarchar)},
          {"replyAuthorLastName", common::DataType(common::DataTypeId::kVarchar)},
          {"isReplyAuthorKnowsOriginalMessageAuthor", common::DataType(common::DataTypeId::kBoolean)}});
  function->bindFunc = bind_is7;
  function->execFunc = exec_is7;
  function::function_set function_set;
  function_set.push_back(std::move(function));
  return function_set;
}

}  // namespace ldbc_ic
}  // namespace extension
}  // namespace neug
