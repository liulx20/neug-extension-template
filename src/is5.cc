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

#include "is5.h"

#include <array>

#include "ldbc_common.h"
#include "neug/execution/common/columns/value_columns.h"
#include "neug/utils/exception/exception.h"

namespace neug {
namespace extension {
namespace ldbc_ic {
namespace {

constexpr size_t kNumOutputColumns = 3;

std::unique_ptr<function::CallFuncInputBase> bind_is5(
    const Schema& /*schema*/, const execution::ContextMeta& /*ctx_meta*/,
    const ::physical::PhysicalPlan& plan, int op_idx) {
  const auto& params =
      plan.plan(op_idx).opr().procedure_call().query().arguments();
  auto input = std::make_unique<IS5FuncInput>();
  ldbc::bind_ldbc_call(plan, op_idx, input.get());
  return input;
}

execution::Context exec_is5(const function::CallFuncInputBase& input,
                            IStorageInterface& graph_iface,
                            const execution::ParamsMap& params) {
  const auto& is5_input = dynamic_cast<const IS5FuncInput&>(input);
  const int64_t message_id = params.at("messageId").GetValue<int64_t>();
  const auto& graph = dynamic_cast<const StorageReadInterface&>(graph_iface);
  const auto& schema = graph.schema();

  const label_t post_label = schema.get_vertex_label_id("POST");
  const label_t comment_label = schema.get_vertex_label_id("COMMENT");
  const label_t person_label = schema.get_vertex_label_id("PERSON");
  const label_t has_creator_label = schema.get_edge_label_id("HASCREATOR");

  auto first_name_col = ldbc::get_vertex_column<std::string_view>(
      graph, person_label, "firstName");
  auto last_name_col = ldbc::get_vertex_column<std::string_view>(
      graph, person_label, "lastName");
  if (!first_name_col || !last_name_col) {
    THROW_RUNTIME_ERROR("is5: failed to load required LDBC property columns");
  }

  vid_t message_vid = StorageReadInterface::kInvalidVid;
  bool is_post = false;
  if (!ldbc::find_message_vertex(graph, post_label, comment_label, message_id,
                                 &message_vid, &is_post)) {
    return execution::Context{};
  }

  const label_t message_label = is_post ? post_label : comment_label;
  const auto has_creator_out = graph.GetGenericOutgoingGraphView(
      message_label, person_label, has_creator_label);
  const vid_t author_vid =
      ldbc::get_single_out_neighbor(has_creator_out, message_vid);
  if (author_vid == StorageReadInterface::kInvalidVid) {
    return execution::Context{};
  }

  execution::ValueColumnBuilder<int64_t> person_id_builder;
  execution::ValueColumnBuilder<std::string> first_name_builder;
  execution::ValueColumnBuilder<std::string> last_name_builder;
  person_id_builder.push_back_opt(
      graph.GetVertexId(person_label, author_vid).GetValue<int64_t>());
  first_name_builder.push_back_opt(
      std::string(first_name_col->get_view(author_vid)));
  last_name_builder.push_back_opt(
      std::string(last_name_col->get_view(author_vid)));

  std::array<std::shared_ptr<execution::IContextColumn>, kNumOutputColumns>
      output_columns;
  output_columns[0] = person_id_builder.finish();
  output_columns[1] = first_name_builder.finish();
  output_columns[2] = last_name_builder.finish();

  return ldbc::make_output_context(
      is5_input.output_aliases,
      {output_columns[0], output_columns[1], output_columns[2]});
}

}  // namespace

function::function_set IS5Function::getFunctionSet() {
  auto function = std::make_unique<function::NeugCallFunction>(
      IS5Function::name,
      std::vector<common::DataTypeId>{common::DataTypeId::kInt64},
      function::call_output_columns{
          {"personId", common::DataTypeId::kInt64},
          {"firstName", common::DataTypeId::kVarchar},
          {"lastName", common::DataTypeId::kVarchar}});
  function->bindFunc = bind_is5;
  function->execFunc = exec_is5;
  function::function_set function_set;
  function_set.push_back(std::move(function));
  return function_set;
}

}  // namespace ldbc_ic
}  // namespace extension
}  // namespace neug
