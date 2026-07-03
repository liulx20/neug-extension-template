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

#include "ic13.h"

#include <cstdint>
#include <queue>
#include <vector>

#include "ldbc_common.h"
#include "neug/execution/common/columns/value_columns.h"
#include "neug/utils/exception/exception.h"

namespace neug {
namespace extension {
namespace ldbc_ic {
namespace {

std::unique_ptr<function::CallFuncInputBase> bind_ic13(
    const Schema& /*schema*/, const execution::ContextMeta& /*ctx_meta*/,
    const ::physical::PhysicalPlan& plan, int op_idx) {
  const auto& params = plan.plan(op_idx).opr().procedure_call().query().arguments();
  if (params.size() < 2) {
    THROW_INVALID_ARGUMENT_EXCEPTION(
        "ic13 requires 2 arguments: person1Id and person2Id");
  }
  auto input = std::make_unique<IC13FuncInput>();
  ldbc::bind_ldbc_call(plan, op_idx, input.get());
  return input;
}

int32_t shortest_path_length(const StorageReadInterface& graph,
                             label_t person_label, label_t knows_label,
                             vid_t src, vid_t dst) {
  if (src == dst) {
    return 0;
  }
  const size_t person_num = graph.GetVertexSet(person_label).size();
  std::vector<int32_t> dist(person_num, -1);
  std::queue<vid_t> q;
  dist[src] = 0;
  q.push(src);

  const auto knows_out = graph.GetGenericOutgoingGraphView(
      person_label, person_label, knows_label);
  const auto knows_in = graph.GetGenericIncomingGraphView(
      person_label, person_label, knows_label);

  while (!q.empty()) {
    const vid_t x = q.front();
    q.pop();
    const auto relax = [&](vid_t v) {
      if (v == StorageReadInterface::kInvalidVid || dist[v] != -1) {
        return;
      }
      dist[v] = dist[x] + 1;
      q.push(v);
    };
    const auto out_edges = knows_out.get_edges(x);
    for (auto it = out_edges.begin(); it != out_edges.end(); ++it) {
      relax(*it);
    }
    const auto in_edges = knows_in.get_edges(x);
    for (auto it = in_edges.begin(); it != in_edges.end(); ++it) {
      relax(*it);
    }
    if (dist[dst] != -1) {
      return dist[dst];
    }
  }
  return -1;
}

execution::Context exec_ic13(const function::CallFuncInputBase& input,
                             IStorageInterface& graph_iface, const execution::ParamsMap& params) {
  const auto& args = dynamic_cast<const IC13FuncInput&>(input);
  const int64_t person1_id = params.at("person1Id").GetValue<int64_t>();
  const int64_t person2_id = params.at("person2Id").GetValue<int64_t>();
  const auto& graph = dynamic_cast<const StorageReadInterface&>(graph_iface);
  const auto& schema = graph.schema();

  const label_t person_label = schema.get_vertex_label_id("PERSON");
  const label_t knows_label = schema.get_edge_label_id("KNOWS");

  vid_t src = StorageReadInterface::kInvalidVid;
  vid_t dst = StorageReadInterface::kInvalidVid;
  int32_t result = 0;
  if (!graph.GetVertexIndex(person_label,
                            execution::Value::INT64(person1_id), src) ||
      !graph.GetVertexIndex(person_label,
                            execution::Value::INT64(person2_id), dst)) {
    result = -1;
  } else {
    result = shortest_path_length(graph, person_label, knows_label, src, dst);
  }

  execution::ValueColumnBuilder<int32_t> length_builder;
  length_builder.push_back_opt(result);
  return ldbc::make_output_context(args.output_aliases, {length_builder.finish()});
}

}  // namespace

function::function_set IC13Function::getFunctionSet() {
  auto fn = std::make_unique<function::NeugCallFunction>(
      IC13Function::name,
      std::vector<common::DataTypeId>{common::DataTypeId::kInt64,
                                      common::DataTypeId::kInt64},
      function::call_output_columns{
          {"shortestPathLength", common::DataTypeId::kInt32}});
  fn->bindFunc = bind_ic13;
  fn->execFunc = exec_ic13;
  function::function_set set;
  set.push_back(std::move(fn));
  return set;
}

}  // namespace ldbc_ic
}  // namespace extension
}  // namespace neug
