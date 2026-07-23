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
#include "neug/common/columns/value_columns.h"
#include "neug/execution/common/context_chunk.h"
#include "neug/utils/exception/exception.h"

namespace neug {
namespace extension {
namespace ldbc {
class IC13 {
 public:
  static std::unique_ptr<function::CallFuncInputBase> bind(
      const Schema& /*schema*/, const execution::ContextMeta& /*ctx_meta*/,
      const ::physical::PhysicalPlan& plan, int op_idx) {
    const auto& params =
        plan.plan(op_idx).opr().procedure_call().query().arguments();
    if (params.size() < 2) {
      THROW_INVALID_ARGUMENT_EXCEPTION(
          "ic13 requires 2 arguments: person1Id and person2Id");
    }
    auto input = std::make_unique<IC13FuncInput>();
    ldbc::bind_ldbc_call(plan, op_idx, *input);
    return input;
  }

  static int32_t shortest_path_length(const StorageReadInterface& graph,
                                      label_t person_label, label_t knows_label,
                                      vid_t src, vid_t dst) {
    if (src == dst) {
      return 0;
    }
    const size_t person_num = graph.GetVertexSet(person_label).size();
    std::vector<int32_t> dist(person_num, 0);
    std::queue<vid_t> q1;
    std::queue<vid_t> q2;
    std::queue<vid_t> tmp;

    dist[src] = 1;
    dist[dst] = -1;
    q1.push(src);
    q2.push(dst);

    const auto person_knows_out = graph.GetGenericOutgoingGraphView(
        person_label, person_label, knows_label);
    const auto person_knows_in = graph.GetGenericIncomingGraphView(
        person_label, person_label, knows_label);

    while (true) {
      if (q1.size() <= q2.size()) {
        if (q1.empty()) {
          break;
        }
        while (!q1.empty()) {
          auto x = q1.front();
          q1.pop();
          const auto& oe = person_knows_out.get_edges(x);
          for (auto it = oe.begin(); it != oe.end(); ++it) {
            auto v = *it;
            if (dist[v] == 0) {
              dist[v] = dist[x] + 1;
              tmp.push(v);
            } else if (dist[v] < 0) {
              return dist[x] - dist[v] - 1;
            }
          }
          const auto& ie = person_knows_in.get_edges(x);
          for (auto it = ie.begin(); it != ie.end(); ++it) {
            auto v = *it;
            if (dist[v] == 0) {
              dist[v] = dist[x] + 1;
              tmp.push(v);
            } else if (dist[v] < 0) {
              return dist[x] - dist[v] - 1;
            }
          }
        }
        std::swap(q1, tmp);
      } else {
        if (q2.empty()) {
          break;
        }
        while (!q2.empty()) {
          auto x = q2.front();
          q2.pop();
          const auto& oe = person_knows_out.get_edges(x);
          for (auto it = oe.begin(); it != oe.end(); ++it) {
            auto v = *it;
            if (dist[v] == 0) {
              dist[v] = dist[x] - 1;
              tmp.push(v);
            } else if (dist[v] > 0) {
              return dist[v] - dist[x] - 1;
            }
          }
          const auto& ie = person_knows_in.get_edges(x);
          for (auto it = ie.begin(); it != ie.end(); ++it) {
            auto v = *it;
            if (dist[v] == 0) {
              dist[v] = dist[x] - 1;
              tmp.push(v);
            } else if (dist[v] > 0) {
              return dist[v] - dist[x] - 1;
            }
          }
        }
        std::swap(q2, tmp);
      }
    }
    return -1;
  }

  static execution::Context exec(const function::CallFuncInputBase& input,
                                 IStorageInterface& graph_iface) {
    const auto& args = dynamic_cast<const IC13FuncInput&>(input);
    const int64_t person1_id = args.person1_id;
    const int64_t person2_id = args.person2_id;
    const auto& graph = dynamic_cast<const StorageReadInterface&>(graph_iface);
    const auto& schema = graph.schema();

    const label_t person_label = schema.get_vertex_label_id("PERSON");
    const label_t knows_label = schema.get_edge_label_id("KNOWS");

    vid_t src = StorageReadInterface::kInvalidVid;
    vid_t dst = StorageReadInterface::kInvalidVid;
    int32_t result = 0;
    if (!graph.GetVertexIndex(person_label, Value::INT64(person1_id), src) ||
        !graph.GetVertexIndex(person_label, Value::INT64(person2_id), dst)) {
      result = -1;
    } else {
      result = shortest_path_length(graph, person_label, knows_label, src, dst);
    }

    ValueColumnBuilder<int32_t> length_builder;
    length_builder.push_back_opt(result);
    execution::ContextChunk chunk;
    chunk.set(0, length_builder.finish());
    execution::Context ctx;
    ctx.append_chunk(std::move(chunk));
    ctx.tag_ids = args.output_aliases;
    return ctx;
  }
};

function::function_set IC13Function::getFunctionSet() {
  auto fn = std::make_unique<function::NeugCallFunction>(
      IC13Function::name,
      function::call_input_types{common::DataType(common::DataTypeId::kInt64),
                                 common::DataType(common::DataTypeId::kInt64)},
      function::call_output_columns{
          {"shortestPathLength",
           common::DataType(common::DataTypeId::kInt32)}});
  fn->bindFunc = IC13::bind;
  fn->execFunc = IC13::exec;
  function::function_set set;
  set.push_back(std::move(fn));
  return set;
}

}  // namespace ldbc
}  // namespace extension
}  // namespace neug
