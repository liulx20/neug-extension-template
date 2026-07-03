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

#include "is4.h"

#include <array>

#include "ldbc_common.h"
#include "neug/execution/common/columns/value_columns.h"
#include "neug/utils/exception/exception.h"

namespace neug {
namespace extension {
namespace ldbc_ic {
namespace {

constexpr size_t kNumOutputColumns = 2;

std::unique_ptr<function::CallFuncInputBase> bind_is4(
    const Schema& /*schema*/, const execution::ContextMeta& /*ctx_meta*/,
    const ::physical::PhysicalPlan& plan, int op_idx) {
  const auto& params = plan.plan(op_idx).opr().procedure_call().query().arguments();
  if (params.size() < 1 || !params[0].has_const_()) {
    THROW_INVALID_ARGUMENT_EXCEPTION("is4: messageId must be an integer literal");
  }
  auto input = std::make_unique<IS4FuncInput>();
  input->message_id = ldbc::parse_i64_arg(params[0].const_(), "messageId");
  ldbc::bind_output_aliases(plan, op_idx, &input->output_aliases);
  return input;
}

execution::Context exec_is4(const function::CallFuncInputBase& input,
                            IStorageInterface& graph_iface) {
  const auto& is4_input = dynamic_cast<const IS4FuncInput&>(input);
  const auto& graph = dynamic_cast<const StorageReadInterface&>(graph_iface);
  const auto& schema = graph.schema();

  const label_t post_label = schema.get_vertex_label_id("POST");
  const label_t comment_label = schema.get_vertex_label_id("COMMENT");

  vid_t message_vid = StorageReadInterface::kInvalidVid;
  bool is_post = false;
  if (!ldbc::find_message_vertex(graph, post_label, comment_label,
                                 is4_input.message_id, &message_vid,
                                 &is_post)) {
    return execution::Context{};
  }

  const label_t message_label = is_post ? post_label : comment_label;
  auto creation_date_col =
      ldbc::get_vertex_column<DateTime>(graph, message_label, "creationDate");
  const int64_t creation_ms =
      creation_date_col
          ? creation_date_col->get_view(message_vid).milli_second
          : 0;
  const std::string content = ldbc::message_content(
      graph, post_label, comment_label, message_vid, is_post);

  execution::ValueColumnBuilder<std::string> content_builder;
  execution::ValueColumnBuilder<DateTime> date_builder;
  content_builder.push_back_opt(content);
  date_builder.push_back_opt(DateTime(creation_ms));

  std::array<std::shared_ptr<execution::IContextColumn>, kNumOutputColumns>
      output_columns;
  output_columns[0] = content_builder.finish();
  output_columns[1] = date_builder.finish();

  return ldbc::make_output_context(is4_input.output_aliases,
                                   {output_columns[0], output_columns[1]});
}

}  // namespace

function::function_set IS4Function::getFunctionSet() {
  auto function = std::make_unique<function::NeugCallFunction>(
      IS4Function::name, std::vector<common::DataTypeId>{common::DataTypeId::kInt64},
      std::vector<std::pair<std::string, common::DataTypeId>>{
          {"messageContent", common::DataTypeId::kVarchar},
          {"messageCreationDate", common::DataTypeId::kTimestampMs}});
  function->bindFunc = bind_is4;
  function->execFunc = exec_is4;
  function::function_set function_set;
  function_set.push_back(std::move(function));
  return function_set;
}

}  // namespace ldbc_ic
}  // namespace extension
}  // namespace neug
