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

#include "ic11.h"

#include <queue>
#include <vector>

#include "ldbc_common.h"
#include "neug/execution/common/context_chunk.h"
#include "neug/common/columns/value_columns.h"
#include "neug/utils/exception/exception.h"

namespace neug {
namespace extension {
namespace ldbc_ic {
class IC11 {
public:
static constexpr size_t kTopN = 10;

struct WorkResult {
  vid_t person_vid = 0;
  int work_from = 0;
  int64_t person_id = 0;
  std::string company_name;
};

struct WorkResultComparer {
  bool operator()(const WorkResult& lhs, const WorkResult& rhs) const {
    if (lhs.work_from < rhs.work_from) {
      return true;
    }
    if (lhs.work_from > rhs.work_from) {
      return false;
    }
    if (lhs.person_id < rhs.person_id) {
      return true;
    }
    if (lhs.person_id > rhs.person_id) {
      return false;
    }
    return lhs.company_name > rhs.company_name;
  }
};

static std::unique_ptr<function::CallFuncInputBase> bind(
    const Schema& /*schema*/, const execution::ContextMeta& /*ctx_meta*/,
    const ::physical::PhysicalPlan& plan, int op_idx) {
  const auto& params =
      plan.plan(op_idx).opr().procedure_call().query().arguments();
  if (params.size() < 3) {
    THROW_INVALID_ARGUMENT_EXCEPTION(
        "ic11 requires 3 arguments: personId, countryName, workFromYear");
  }
  auto input = std::make_unique<IC11FuncInput>();
  ldbc::bind_ldbc_call(plan, op_idx, input.get());
  return input;
}

static execution::Context exec(const function::CallFuncInputBase& input,
                             IStorageInterface& graph_iface) {
  const auto& args = dynamic_cast<const IC11FuncInput&>(input);
  const int64_t person_id = args.person_id;
  const std::string country_name =
      args.country_name;
  const int32_t work_from_year = args.work_from_year;
  const auto& graph = dynamic_cast<const StorageReadInterface&>(graph_iface);
  const auto& schema = graph.schema();

  const label_t person_label = schema.get_vertex_label_id("PERSON");
  const label_t place_label = schema.get_vertex_label_id("PLACE");
  const label_t org_label = schema.get_vertex_label_id("ORGANISATION");
  const label_t knows_label = schema.get_edge_label_id("KNOWS");
  const label_t work_at_label = schema.get_edge_label_id("WORKAT");
  const label_t is_located_in_label = schema.get_edge_label_id("ISLOCATEDIN");

  auto first_name_col = ldbc::get_vertex_column<std::string_view>(
      graph, person_label, "firstName");
  auto last_name_col = ldbc::get_vertex_column<std::string_view>(
      graph, person_label, "lastName");
  auto org_name_col =
      ldbc::get_vertex_column<std::string_view>(graph, org_label, "name");
  auto person_id_col = ldbc::get_vertex_column<int64_t>(graph, person_label, "id");
  if (!first_name_col || !last_name_col || !org_name_col) {
    THROW_RUNTIME_ERROR("ic11: failed to load required LDBC property columns");
  }

  vid_t root = StorageReadInterface::kInvalidVid;
  if (!graph.GetVertexIndex(person_label, Value::INT64(person_id),
                            root)) {
    return execution::Context{};
  }

  const vid_t country_vid = ldbc::find_vertex_by_string_prop(
      graph, place_label, "name", country_name);
  if (country_vid == StorageReadInterface::kInvalidVid) {
    return execution::Context{};
  }

  std::vector<bool> friends;
  ldbc::mark_knows_1d_2d_neighbors(graph, person_label, knows_label, root,
                                   &friends);

  const auto org_located_in_in = graph.GetGenericIncomingGraphView(
      place_label, org_label, is_located_in_label);
  const auto person_work_at_in =
      graph.GetGenericIncomingGraphView(org_label, person_label, work_at_label);

 
  const auto& work_from_accessor = graph.GetEdgeDataAccessor(person_label, org_label,
                                                   work_at_label, "workFrom");
  

  std::priority_queue<WorkResult, std::vector<WorkResult>, WorkResultComparer>
      pq;
  const auto orgs = org_located_in_in.get_edges(country_vid);
  for (auto oit = orgs.begin(); oit != orgs.end(); ++oit) {
    const vid_t company_vid = *oit;
    const std::string company_name(org_name_col->get_view(company_vid));
    const auto workers = person_work_at_in.get_edges(company_vid);
    for (auto wit = workers.begin(); wit != workers.end(); ++wit) {
      const vid_t person_vid = *wit;
   
      const int32_t work_from = work_from_accessor.get_typed_data<int32_t>(wit);
      
      if (work_from >= work_from_year) {
        continue;
      }
      if (!friends[person_vid]) {
        continue;
      }
      const int64_t person_id =
          person_id_col->get_view(person_vid);
      WorkResult row{person_vid, work_from, person_id, company_name};
      if (pq.size() < kTopN) {
        pq.push(row);
        continue;
      }
      const auto& top = pq.top();
      if (row.work_from < top.work_from ||
          (row.work_from == top.work_from &&
           (row.person_id < top.person_id ||
            (row.person_id == top.person_id &&
             row.company_name > top.company_name)))) {
        pq.pop();
        pq.push(row);
      }
    }
  }

  std::vector<WorkResult> results;
  results.reserve(pq.size());
  while (!pq.empty()) {
    results.push_back(pq.top());
    pq.pop();
  }

  ValueColumnBuilder<int64_t> person_id_builder;
  ValueColumnBuilder<std::string> first_name_builder;
  ValueColumnBuilder<std::string> last_name_builder;
  ValueColumnBuilder<std::string> org_name_builder;
  ValueColumnBuilder<int32_t> work_from_builder;

  for (size_t i = results.size(); i > 0; --i) {
    const auto& row = results[i - 1];
    person_id_builder.push_back_opt(row.person_id);
    first_name_builder.push_back_opt(
        std::string(first_name_col->get_view(row.person_vid)));
    last_name_builder.push_back_opt(
        std::string(last_name_col->get_view(row.person_vid)));
    org_name_builder.push_back_opt(row.company_name);
    work_from_builder.push_back_opt(row.work_from);
  }

  execution::ContextChunk chunk;
  chunk.set(0, person_id_builder.finish());
  chunk.set(1, first_name_builder.finish());
  chunk.set(2, last_name_builder.finish());
  chunk.set(3, org_name_builder.finish());
  chunk.set(4, work_from_builder.finish());
  execution::Context ctx;
  ctx.append_chunk(std::move(chunk));
  ctx.tag_ids = args.output_aliases;
  return ctx;
}

};

function::function_set IC11Function::getFunctionSet() {
  auto fn = std::make_unique<function::NeugCallFunction>(
      IC11Function::name,
      function::call_input_types{common::DataType(common::DataTypeId::kInt64),
                                      common::DataType(common::DataTypeId::kVarchar),
                                      common::DataType(common::DataTypeId::kInt64)},
      function::call_output_columns{
          {"personId", common::DataType(common::DataTypeId::kInt64)},
          {"personFirstName", common::DataType(common::DataTypeId::kVarchar)},
          {"personLastName", common::DataType(common::DataTypeId::kVarchar)},
          {"organizationName", common::DataType(common::DataTypeId::kVarchar)},
          {"organizationWorkFromYear", common::DataType(common::DataTypeId::kInt32)}});
  fn->bindFunc = IC11::bind;
  fn->execFunc = IC11::exec;
  function::function_set set;
  set.push_back(std::move(fn));
  return set;
}

}  // namespace ldbc_ic
}  // namespace extension
}  // namespace neug
