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

#include "ic6.h"

#include <array>
#include <queue>
#include <string>
#include <vector>

#include "ldbc_common.h"
#include "neug/execution/common/columns/value_columns.h"
#include "neug/utils/exception/exception.h"

namespace neug {
namespace extension {
namespace ldbc_ic {
namespace {

constexpr size_t kTopN = 10;
constexpr size_t kNumOutputColumns = 2;

struct TagResult {
  int count = 0;
  std::string_view tag_name;
};

struct TagResultComparer {
  bool operator()(const TagResult& lhs, const TagResult& rhs) const {
    if (lhs.count > rhs.count) {
      return true;
    }
    if (lhs.count < rhs.count) {
      return false;
    }
    return lhs.tag_name < rhs.tag_name;
  }
};

void try_push_tag(std::priority_queue<TagResult, std::vector<TagResult>,
                                      TagResultComparer>* heap,
                  int count, std::string_view tag_name) {
  if (count == 0) {
    return;
  }
  if (heap->size() < kTopN) {
    heap->push({count, tag_name});
    return;
  }
  const auto& top = heap->top();
  if (count > top.count || (count == top.count && tag_name < top.tag_name)) {
    heap->pop();
    heap->push({count, tag_name});
  }
}

std::unique_ptr<function::CallFuncInputBase> bind_ic6(
    const Schema& /*schema*/, const execution::ContextMeta& /*ctx_meta*/,
    const ::physical::PhysicalPlan& plan, int op_idx) {
  const auto& params =
      plan.plan(op_idx).opr().procedure_call().query().arguments();
  if (params.size() < 2) {
    THROW_INVALID_ARGUMENT_EXCEPTION(
        "ic6 requires 2 arguments: personId and tagName");
  }
  auto input = std::make_unique<IC6FuncInput>();
  ldbc::bind_ldbc_call(plan, op_idx, input.get());
  return input;
}

execution::Context exec_ic6(const function::CallFuncInputBase& input,
                            IStorageInterface& graph_iface,
                            const execution::ParamsMap& params) {
  const auto& ic6_input = dynamic_cast<const IC6FuncInput&>(input);
  const int64_t person_id = params.at("personId").GetValue<int64_t>();
  const std::string tag_name = params.at("tagName").GetValue<std::string>();
  const auto& graph = dynamic_cast<const StorageReadInterface&>(graph_iface);
  const auto& schema = graph.schema();

  const label_t tag_label = schema.get_vertex_label_id("TAG");
  const label_t person_label = schema.get_vertex_label_id("PERSON");
  const label_t post_label = schema.get_vertex_label_id("POST");
  const label_t knows_label = schema.get_edge_label_id("KNOWS");
  const label_t has_creator_label = schema.get_edge_label_id("HASCREATOR");
  const label_t has_tag_label = schema.get_edge_label_id("HASTAG");

  auto tag_name_col =
      ldbc::get_vertex_column<std::string_view>(graph, tag_label, "name");
  if (!tag_name_col) {
    THROW_RUNTIME_ERROR("ic6: failed to load required LDBC property columns");
  }

  vid_t root = StorageReadInterface::kInvalidVid;
  if (!graph.GetVertexIndex(person_label, execution::Value::INT64(person_id),
                            root)) {
    return execution::Context{};
  }

  const vid_t tag_vid =
      ldbc::find_vertex_by_string_prop(graph, tag_label, "name", tag_name);
  if (tag_vid == StorageReadInterface::kInvalidVid) {
    return execution::Context{};
  }

  std::vector<bool> friends;
  ldbc::mark_knows_1d_2d_neighbors(graph, person_label, knows_label, root,
                                   &friends);

  const size_t tag_num = graph.GetVertexSet(tag_label).size();
  std::vector<int> post_count(tag_num, 0);

  const auto post_has_tag_in =
      graph.GetGenericIncomingGraphView(tag_label, post_label, has_tag_label);
  const auto post_has_creator_out = graph.GetGenericOutgoingGraphView(
      post_label, person_label, has_creator_label);
  const auto post_has_tag_out =
      graph.GetGenericOutgoingGraphView(post_label, tag_label, has_tag_label);

  const auto tagged_posts = post_has_tag_in.get_edges(tag_vid);
  for (auto it = tagged_posts.begin(); it != tagged_posts.end(); ++it) {
    const vid_t post_vid = *it;
    const vid_t creator_vid =
        ldbc::get_single_out_neighbor(post_has_creator_out, post_vid);
    if (creator_vid == StorageReadInterface::kInvalidVid ||
        creator_vid >= friends.size() || !friends[creator_vid]) {
      continue;
    }
    const auto tags = post_has_tag_out.get_edges(post_vid);
    for (auto tag_it = tags.begin(); tag_it != tags.end(); ++tag_it) {
      if (*tag_it < tag_num) {
        ++post_count[*tag_it];
      }
    }
  }
  if (tag_vid < tag_num) {
    post_count[tag_vid] = 0;
  }

  std::priority_queue<TagResult, std::vector<TagResult>, TagResultComparer>
      heap;
  for (vid_t other_tag_vid = 0; other_tag_vid < tag_num; ++other_tag_vid) {
    try_push_tag(&heap, post_count[other_tag_vid],
                 tag_name_col->get_view(other_tag_vid));
  }

  std::vector<TagResult> results;
  results.reserve(heap.size());
  while (!heap.empty()) {
    results.push_back(heap.top());
    heap.pop();
  }

  execution::ValueColumnBuilder<std::string> tag_name_builder;
  execution::ValueColumnBuilder<int32_t> post_count_builder;
  tag_name_builder.reserve(results.size());
  post_count_builder.reserve(results.size());
  for (size_t i = results.size(); i > 0; --i) {
    tag_name_builder.push_back_opt(std::string(results[i - 1].tag_name));
    post_count_builder.push_back_opt(results[i - 1].count);
  }

  std::array<std::shared_ptr<execution::IContextColumn>, kNumOutputColumns>
      output_columns;
  output_columns[0] = tag_name_builder.finish();
  output_columns[1] = post_count_builder.finish();
  return ldbc::make_output_context(ic6_input.output_aliases,
                                   {output_columns[0], output_columns[1]});
}

}  // namespace

function::function_set IC6Function::getFunctionSet() {
  auto function = std::make_unique<function::NeugCallFunction>(
      IC6Function::name,
      std::vector<common::DataTypeId>{common::DataTypeId::kInt64,
                                      common::DataTypeId::kVarchar},
      function::call_output_columns{{"tagName", common::DataTypeId::kVarchar},
                                    {"postCount", common::DataTypeId::kInt32}});
  function->bindFunc = bind_ic6;
  function->execFunc = exec_ic6;
  function::function_set function_set;
  function_set.push_back(std::move(function));
  return function_set;
}

}  // namespace ldbc_ic
}  // namespace extension
}  // namespace neug
