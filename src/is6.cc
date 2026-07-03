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

#include "is6.h"

#include <array>

#include "ldbc_common.h"
#include "neug/execution/common/columns/value_columns.h"
#include "neug/utils/exception/exception.h"

namespace neug {
namespace extension {
namespace ldbc_ic {
namespace {

constexpr size_t kNumOutputColumns = 5;

std::unique_ptr<function::CallFuncInputBase> bind_is6(
    const Schema& /*schema*/, const execution::ContextMeta& /*ctx_meta*/,
    const ::physical::PhysicalPlan& plan, int op_idx) {
  const auto& params = plan.plan(op_idx).opr().procedure_call().query().arguments();
  if (params.size() < 1 || !params[0].has_const_()) {
    THROW_INVALID_ARGUMENT_EXCEPTION("is6: messageId must be an integer literal");
  }
  auto input = std::make_unique<IS6FuncInput>();
  input->message_id = ldbc::parse_i64_arg(params[0].const_(), "messageId");
  ldbc::bind_output_aliases(plan, op_idx, &input->output_aliases);
  return input;
}

execution::Context exec_is6(const function::CallFuncInputBase& input,
                            IStorageInterface& graph_iface) {
  const auto& is6_input = dynamic_cast<const IS6FuncInput&>(input);
  const auto& graph = dynamic_cast<const StorageReadInterface&>(graph_iface);
  const auto& schema = graph.schema();

  const label_t forum_label = schema.get_vertex_label_id("FORUM");
  const label_t person_label = schema.get_vertex_label_id("PERSON");
  const label_t post_label = schema.get_vertex_label_id("POST");
  const label_t comment_label = schema.get_vertex_label_id("COMMENT");
  const label_t reply_of_label = schema.get_edge_label_id("REPLYOF");
  const label_t container_of_label = schema.get_edge_label_id("CONTAINEROF");
  const label_t has_moderator_label = schema.get_edge_label_id("HASMODERATOR");

  auto first_name_col =
      ldbc::get_vertex_column<std::string_view>(graph, person_label, "firstName");
  auto last_name_col =
      ldbc::get_vertex_column<std::string_view>(graph, person_label, "lastName");
  auto forum_title_col =
      ldbc::get_vertex_column<std::string_view>(graph, forum_label, "title");
  if (!first_name_col || !last_name_col || !forum_title_col) {
    THROW_RUNTIME_ERROR("is6: failed to load required LDBC property columns");
  }

  vid_t message_vid = StorageReadInterface::kInvalidVid;
  bool is_post = false;
  if (!ldbc::find_message_vertex(graph, post_label, comment_label,
                                 is6_input.message_id, &message_vid,
                                 &is_post)) {
    return execution::Context{};
  }

  const vid_t post_vid = ldbc::resolve_root_post(
      graph, post_label, comment_label, reply_of_label, message_vid, is_post);
  if (post_vid == StorageReadInterface::kInvalidVid) {
    return execution::Context{};
  }

  const auto forum_container_of_post_in = graph.GetGenericIncomingGraphView(
      post_label, forum_label, container_of_label);
  const vid_t forum_vid =
      ldbc::get_single_out_neighbor(forum_container_of_post_in, post_vid);
  if (forum_vid == StorageReadInterface::kInvalidVid) {
    return execution::Context{};
  }

  const auto forum_has_moderator_out = graph.GetGenericOutgoingGraphView(
      forum_label, person_label, has_moderator_label);
  const vid_t moderator_vid =
      ldbc::get_single_out_neighbor(forum_has_moderator_out, forum_vid);
  if (moderator_vid == StorageReadInterface::kInvalidVid) {
    return execution::Context{};
  }

  execution::ValueColumnBuilder<int64_t> forum_id_builder;
  execution::ValueColumnBuilder<std::string> forum_title_builder;
  execution::ValueColumnBuilder<int64_t> moderator_id_builder;
  execution::ValueColumnBuilder<std::string> moderator_first_builder;
  execution::ValueColumnBuilder<std::string> moderator_last_builder;

  forum_id_builder.push_back_opt(
      graph.GetVertexId(forum_label, forum_vid).GetValue<int64_t>());
  forum_title_builder.push_back_opt(
      std::string(forum_title_col->get_view(forum_vid)));
  moderator_id_builder.push_back_opt(
      graph.GetVertexId(person_label, moderator_vid).GetValue<int64_t>());
  moderator_first_builder.push_back_opt(
      std::string(first_name_col->get_view(moderator_vid)));
  moderator_last_builder.push_back_opt(
      std::string(last_name_col->get_view(moderator_vid)));

  std::array<std::shared_ptr<execution::IContextColumn>, kNumOutputColumns>
      output_columns;
  output_columns[0] = forum_id_builder.finish();
  output_columns[1] = forum_title_builder.finish();
  output_columns[2] = moderator_id_builder.finish();
  output_columns[3] = moderator_first_builder.finish();
  output_columns[4] = moderator_last_builder.finish();

  return ldbc::make_output_context(is6_input.output_aliases,
                                   {output_columns[0], output_columns[1],
                                    output_columns[2], output_columns[3],
                                    output_columns[4]});
}

}  // namespace

function::function_set IS6Function::getFunctionSet() {
  auto function = std::make_unique<function::NeugCallFunction>(
      IS6Function::name, std::vector<common::DataTypeId>{common::DataTypeId::kInt64},
      std::vector<std::pair<std::string, common::DataTypeId>>{
          {"forumId", common::DataTypeId::kInt64},
          {"forumTitle", common::DataTypeId::kVarchar},
          {"moderatorId", common::DataTypeId::kInt64},
          {"moderatorFirstName", common::DataTypeId::kVarchar},
          {"moderatorLastName", common::DataTypeId::kVarchar}});
  function->bindFunc = bind_is6;
  function->execFunc = exec_is6;
  function::function_set function_set;
  function_set.push_back(std::move(function));
  return function_set;
}

}  // namespace ldbc_ic
}  // namespace extension
}  // namespace neug
