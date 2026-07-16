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

#include "ic4.h"

#include <queue>
#include <string>
#include <vector>

#include "ldbc_common.h"
#include "neug/execution/common/context_chunk.h"
#include "neug/common/columns/value_columns.h"
#include "neug/utils/exception/exception.h"

namespace neug {
namespace extension {
namespace ldbc_ic {
class IC4 {
public:
static constexpr size_t kTopN = 10;
static constexpr int64_t kMillisPerDay = 24L * 60 * 60 * 1000;

struct TagRow {
  int count = 0;
  std::string_view name;
};

struct TagRowCmp {
  bool operator()(const TagRow& a, const TagRow& b) const {
    if (a.count != b.count) {
      return a.count > b.count;
    }
    return a.name < b.name;
  }
};

static void try_push_tag(
    std::priority_queue<TagRow, std::vector<TagRow>, TagRowCmp>* heap,
    int count, std::string_view name) {
  if (count == 0) {
    return;
  }
  if (heap->size() < kTopN) {
    heap->push({count, name});
    return;
  }
  const auto& top = heap->top();
  if (count > top.count || (count == top.count && name < top.name)) {
    heap->pop();
    heap->push({count, name});
  }
}

static std::unique_ptr<function::CallFuncInputBase> bind(
    const Schema& /*schema*/, const execution::ContextMeta& /*ctx_meta*/,
    const ::physical::PhysicalPlan& plan, int op_idx) {
  const auto& params =
      plan.plan(op_idx).opr().procedure_call().query().arguments();
  if (params.size() < 3) {
    THROW_INVALID_ARGUMENT_EXCEPTION(
        "ic4 requires 3 arguments: personId, startDate, durationDays");
  }
  auto input = std::make_unique<IC4FuncInput>();
  ldbc::bind_ldbc_call(plan, op_idx, input.get());
  return input;
}

static execution::Context exec(const function::CallFuncInputBase& input,
                            IStorageInterface& graph_iface) {
  const auto& args = dynamic_cast<const IC4FuncInput&>(input);
  const int64_t person_id = args.person_id;
  const int64_t start_date_ms = args.start_date_ms;
  const int64_t duration_days = args.duration_days;
  const auto& graph = dynamic_cast<const StorageReadInterface&>(graph_iface);
  const auto& schema = graph.schema();

  const label_t person_label = schema.get_vertex_label_id("PERSON");
  const label_t post_label = schema.get_vertex_label_id("POST");
  const label_t tag_label = schema.get_vertex_label_id("TAG");
  const label_t knows_label = schema.get_edge_label_id("KNOWS");
  const label_t has_creator_label = schema.get_edge_label_id("HASCREATOR");
  const label_t has_tag_label = schema.get_edge_label_id("HASTAG");

  auto tag_name_col =
      ldbc::get_vertex_column<std::string_view>(graph, tag_label, "name");
  if (!tag_name_col) {
    THROW_RUNTIME_ERROR("ic4: failed to load TAG.name column");
  }

  vid_t root = StorageReadInterface::kInvalidVid;
  if (!graph.GetVertexIndex(person_label, Value::INT64(person_id),
                            root)) {
    return execution::Context{};
  }

  const int64_t end_ms = start_date_ms + duration_days * kMillisPerDay;

  const auto post_in = graph.GetGenericIncomingGraphView(
      person_label, post_label, has_creator_label);
  const auto post_tag_out =
      graph.GetGenericOutgoingGraphView(post_label, tag_label, has_tag_label);

  auto post_date_col =
      ldbc::get_vertex_column<DateTime>(graph, post_label, "creationDate");
  if (!post_date_col) {
    THROW_RUNTIME_ERROR("ic4: failed to load POST.creationDate column");
  }

  const size_t tag_num = graph.GetVertexSet(tag_label).size();
  std::vector<bool> neg(tag_num, false);
  std::vector<int> tag_count(tag_num, 0);

  ldbc::foreach_knows_neighbor(
      graph, person_label, knows_label, root, [&](vid_t friend_vid) {
        const auto posts = post_in.get_edges(friend_vid);
        for (auto pit = posts.begin(); pit != posts.end(); ++pit) {
          const vid_t post_vid = *pit;
          const int64_t created =
              post_date_col->get_view(post_vid).milli_second;
          if (created >= end_ms) {
            continue;
          }
          const auto tags = post_tag_out.get_edges(post_vid);
          if (created >= start_date_ms) {
            for (auto tit = tags.begin(); tit != tags.end(); ++tit) {
              const vid_t tag_vid = *tit;
              if (tag_vid < tag_num && !neg[tag_vid]) {
                ++tag_count[tag_vid];
              }
            }
          } else {
            for (auto tit = tags.begin(); tit != tags.end(); ++tit) {
              const vid_t tag_vid = *tit;
              if (tag_vid < tag_num) {
                neg[tag_vid] = true;
                tag_count[tag_vid] = 0;
              }
            }
          }
        }
      });

  std::priority_queue<TagRow, std::vector<TagRow>, TagRowCmp> heap;
  for (vid_t tag_vid = 0; tag_vid < tag_num; ++tag_vid) {
    const int count = tag_count[tag_vid];
    if (count == 0) {
      continue;
    }
    try_push_tag(&heap, count, tag_name_col->get_view(tag_vid));
  }

  std::vector<TagRow> rows;
  rows.reserve(heap.size());
  while (!heap.empty()) {
    rows.push_back(heap.top());
    heap.pop();
  }

  ValueColumnBuilder<std::string> name_builder;
  ValueColumnBuilder<int32_t> count_builder;
  name_builder.reserve(rows.size());
  count_builder.reserve(rows.size());
  for (size_t i = rows.size(); i > 0; --i) {
    name_builder.push_back_opt(std::string(rows[i - 1].name));
    count_builder.push_back_opt(rows[i - 1].count);
  }

  execution::ContextChunk chunk;
  chunk.set(0, name_builder.finish());
  chunk.set(1, count_builder.finish());
  execution::Context ctx;
  ctx.append_chunk(std::move(chunk));
  ctx.tag_ids = args.output_aliases;
  return ctx;
}

};

function::function_set IC4Function::getFunctionSet() {
  auto fn = std::make_unique<function::NeugCallFunction>(
      IC4Function::name,
      function::call_input_types{common::DataType(common::DataTypeId::kInt64),
                                      common::DataType(common::DataTypeId::kInt64),
                                      common::DataType(common::DataTypeId::kInt64)},
      function::call_output_columns{{"tagName", common::DataType(common::DataTypeId::kVarchar)},
                                    {"postCount", common::DataType(common::DataTypeId::kInt32)}});
  fn->bindFunc = IC4::bind;
  fn->execFunc = IC4::exec;
  function::function_set set;
  set.push_back(std::move(fn));
  return set;
}

}  // namespace ldbc_ic
}  // namespace extension
}  // namespace neug
