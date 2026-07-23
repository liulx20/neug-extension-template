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

#include "ldbc_common.h"
#include "neug/common/columns/value_columns.h"
#include "neug/execution/common/context_chunk.h"
#include "neug/utils/exception/exception.h"

namespace neug {
namespace extension {
namespace ldbc {
class IS4 {
 public:
  static std::unique_ptr<function::CallFuncInputBase> bind(
      const Schema& /*schema*/, const execution::ContextMeta& /*ctx_meta*/,
      const ::physical::PhysicalPlan& plan, int op_idx) {
    auto input = std::make_unique<IS4FuncInput>();
    ldbc::bind_ldbc_call(plan, op_idx, *input);
    return input;
  }

  static execution::Context exec(const function::CallFuncInputBase& input,
                                 IStorageInterface& graph_iface) {
    const auto& is4_input = dynamic_cast<const IS4FuncInput&>(input);
    const int64_t message_id = is4_input.message_id;
    const auto& graph = dynamic_cast<const StorageReadInterface&>(graph_iface);
    const auto& schema = graph.schema();

    const label_t post_label = schema.get_vertex_label_id("POST");
    const label_t comment_label = schema.get_vertex_label_id("COMMENT");

    vid_t message_vid = StorageReadInterface::kInvalidVid;
    bool is_post = false;
    if (!ldbc::find_message_vertex(graph, post_label, comment_label, message_id,
                                   message_vid, is_post)) {
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

    ValueColumnBuilder<std::string> content_builder;
    ValueColumnBuilder<DateTime> date_builder;
    content_builder.push_back_opt(content);
    date_builder.push_back_opt(DateTime(creation_ms));

    execution::ContextChunk chunk;
    chunk.set(0, content_builder.finish());
    chunk.set(1, date_builder.finish());
    execution::Context ctx;
    ctx.append_chunk(std::move(chunk));
    ctx.tag_ids = is4_input.output_aliases;
    return ctx;
  }
};

function::function_set IS4Function::getFunctionSet() {
  auto function = std::make_unique<function::NeugCallFunction>(
      IS4Function::name,
      function::call_input_types{common::DataType(common::DataTypeId::kInt64)},
      function::call_output_columns{
          {"messageContent", common::DataType(common::DataTypeId::kVarchar)},
          {"messageCreationDate",
           common::DataType(common::DataTypeId::kTimestampMs)}});
  function->bindFunc = IS4::bind;
  function->execFunc = IS4::exec;
  function::function_set function_set;
  function_set.push_back(std::move(function));
  return function_set;
}

}  // namespace ldbc
}  // namespace extension
}  // namespace neug
