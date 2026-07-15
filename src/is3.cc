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

#include "is3.h"

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

constexpr size_t kNumOutputColumns = 4;

struct FriendInfo {
  vid_t person_vid = 0;
  int64_t person_id = 0;
  int64_t friendship_creation_ms = 0;
};

std::unique_ptr<function::CallFuncInputBase> bind_is3(
    const Schema& /*schema*/, const execution::ContextMeta& /*ctx_meta*/,
    const ::physical::PhysicalPlan& plan, int op_idx) {
  const auto& params =
      plan.plan(op_idx).opr().procedure_call().query().arguments();
  auto input = std::make_unique<IS3FuncInput>();
  ldbc::bind_ldbc_call(plan, op_idx, input.get());
  return input;
}

execution::Context exec_is3(const function::CallFuncInputBase& input,
                            IStorageInterface& graph_iface,
                            const execution::ParamsMap& params) {
  const auto& is3_input = dynamic_cast<const IS3FuncInput&>(input);
  const int64_t person_id = params.at("personId").GetValue<int64_t>();
  const auto& graph = dynamic_cast<const StorageReadInterface&>(graph_iface);
  const auto& schema = graph.schema();

  const label_t person_label = schema.get_vertex_label_id("PERSON");
  const label_t knows_label = schema.get_edge_label_id("KNOWS");

  auto first_name_col = ldbc::get_vertex_column<std::string_view>(
      graph, person_label, "firstName");
  auto last_name_col = ldbc::get_vertex_column<std::string_view>(
      graph, person_label, "lastName");
  auto person_id_col = ldbc::get_vertex_column<int64_t>(graph, person_label, "id");
  if (!first_name_col || !last_name_col) {
    THROW_RUNTIME_ERROR("is3: failed to load required LDBC property columns");
  }

  vid_t person_vid = StorageReadInterface::kInvalidVid;
  if (!graph.GetVertexIndex(person_label, execution::Value::INT64(person_id),
                            person_vid)) {
    return execution::Context{};
  }

  const auto out_view = graph.GetGenericOutgoingGraphView(
      person_label, person_label, knows_label);
  const auto in_view = graph.GetGenericIncomingGraphView(
      person_label, person_label, knows_label);
  const auto edge_accessor = graph.GetEdgeDataAccessor(
      person_label, person_label, knows_label, "creationDate");

  std::vector<FriendInfo> friends;
  auto collect = [&](const CsrView& view, vid_t root) {
    for (auto it = view.get_edges(root).begin();
         it != view.get_edges(root).end(); ++it) {
      FriendInfo info;
      info.person_vid = *it;
      info.person_id = person_id_col->get_view(info.person_vid);
      info.friendship_creation_ms =
          edge_accessor.get_typed_data<DateTime>(it).milli_second;
      friends.push_back(info);
    }
  };
  collect(out_view, person_vid);
  collect(in_view, person_vid);

  std::sort(friends.begin(), friends.end(),
            [](const FriendInfo& lhs, const FriendInfo& rhs) {
              if (lhs.friendship_creation_ms != rhs.friendship_creation_ms) {
                return lhs.friendship_creation_ms > rhs.friendship_creation_ms;
              }
              return lhs.person_id < rhs.person_id;
            });

  execution::ValueColumnBuilder<int64_t> person_id_builder;
  execution::ValueColumnBuilder<std::string> first_name_builder;
  execution::ValueColumnBuilder<std::string> last_name_builder;
  execution::ValueColumnBuilder<DateTime> friendship_date_builder;
  person_id_builder.reserve(friends.size());
  first_name_builder.reserve(friends.size());
  last_name_builder.reserve(friends.size());
  friendship_date_builder.reserve(friends.size());

  for (const auto& friend_info : friends) {
    person_id_builder.push_back_opt(friend_info.person_id);
    first_name_builder.push_back_opt(
        std::string(first_name_col->get_view(friend_info.person_vid)));
    last_name_builder.push_back_opt(
        std::string(last_name_col->get_view(friend_info.person_vid)));
    friendship_date_builder.push_back_opt(
        DateTime(friend_info.friendship_creation_ms));
  }

  std::array<std::shared_ptr<execution::IContextColumn>, kNumOutputColumns>
      output_columns;
  output_columns[0] = person_id_builder.finish();
  output_columns[1] = first_name_builder.finish();
  output_columns[2] = last_name_builder.finish();
  output_columns[3] = friendship_date_builder.finish();

  return ldbc::make_output_context(is3_input.output_aliases,
                                   {output_columns[0], output_columns[1],
                                    output_columns[2], output_columns[3]});
}

}  // namespace

function::function_set IS3Function::getFunctionSet() {
  auto function = std::make_unique<function::NeugCallFunction>(
      IS3Function::name,
      std::vector<common::DataTypeId>{common::DataTypeId::kInt64},
      function::call_output_columns{
          {"personId", common::DataTypeId::kInt64},
          {"firstName", common::DataTypeId::kVarchar},
          {"lastName", common::DataTypeId::kVarchar},
          {"friendshipCreationDate", common::DataTypeId::kTimestampMs}});
  function->bindFunc = bind_is3;
  function->execFunc = exec_is3;
  function::function_set function_set;
  function_set.push_back(std::move(function));
  return function_set;
}

}  // namespace ldbc_ic
}  // namespace extension
}  // namespace neug
