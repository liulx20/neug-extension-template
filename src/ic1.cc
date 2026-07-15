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

#include "ic1.h"

#include <queue>
#include <string_view>
#include <vector>

#include "ldbc_common.h"
#include "neug/common/extra_type_info.h"
#include "neug/compiler/binder/binder.h"
#include "neug/compiler/function/table/bind_data.h"
#include "neug/compiler/function/table/bind_input.h"
#include "neug/compiler/function/table/table_function.h"
#include "neug/execution/common/columns/list_columns.h"
#include "neug/execution/common/columns/value_columns.h"
#include "neug/utils/exception/exception.h"

namespace neug {
namespace extension {
namespace ldbc_ic {
namespace {

constexpr size_t kTopN = 20;

struct PersonResult {
  uint8_t distance = 0;
  std::string_view last_name;
  int64_t id = 0;
  vid_t vid = 0;
};

struct PersonResultComparer {
  bool operator()(const PersonResult& lhs, const PersonResult& rhs) const {
    if (lhs.distance < rhs.distance) {
      return true;
    }
    if (lhs.distance > rhs.distance) {
      return false;
    }
    if (lhs.last_name < rhs.last_name) {
      return true;
    }
    if (lhs.last_name > rhs.last_name) {
      return false;
    }
    return lhs.id < rhs.id;
  }
};

const DataType kOrgPlaceTupleType = DataType::Struct(
    {DataType(DataTypeId::kVarchar), DataType(DataTypeId::kInt32),
     DataType(DataTypeId::kVarchar)});

const common::DataType kOrgPlaceTupleCatalogType =
    common::DataType::Struct({common::DataType(common::DataTypeId::kVarchar),
                              common::DataType(common::DataTypeId::kInt32),
                              common::DataType(common::DataTypeId::kVarchar)});

void try_enqueue_friend(
    std::priority_queue<PersonResult, std::vector<PersonResult>,
                        PersonResultComparer>* pq,
    int dist, std::string_view last_name, int64_t id, vid_t vid) {
  if (pq->size() < kTopN) {
    pq->push({static_cast<uint8_t>(dist), last_name, id, vid});
    return;
  }
  const auto& top = pq->top();
  if (dist != top.distance) {
    return;
  }
  if (last_name < top.last_name) {
    pq->pop();
    pq->push({static_cast<uint8_t>(dist), last_name, id, vid});
  } else if (last_name == top.last_name && id < top.id) {
    pq->pop();
    pq->push({static_cast<uint8_t>(dist), last_name, id, vid});
  }
}

void collect_friends(
    const StorageReadInterface& graph, label_t person_label,
    label_t knows_label, vid_t root, std::string_view first_name,
    const StorageReadInterface::vertex_column_t<std::string_view>&
        first_name_col,
    const StorageReadInterface::vertex_column_t<std::string_view>&
        last_name_col,
    const StorageReadInterface::vertex_column_t<int64_t>& person_id_col,
    std::vector<PersonResult>* results) {
  const auto knows_out = graph.GetGenericOutgoingGraphView(
      person_label, person_label, knows_label);
  const auto knows_in = graph.GetGenericIncomingGraphView(
      person_label, person_label, knows_label);

  const size_t person_num = graph.GetVertexSet(person_label).size();
  std::vector<bool> accessed(person_num, false);
  std::vector<vid_t> d1_friends;
  std::vector<vid_t> d2_friends;
  std::priority_queue<PersonResult, std::vector<PersonResult>,
                      PersonResultComparer>
      pq;

  accessed[root] = true;
  const auto root_in = knows_in.get_edges(root);
  for (auto it = root_in.begin(); it != root_in.end(); ++it) {
    const vid_t v = *it;
    d1_friends.push_back(v);
    accessed[v] = true;
    if (first_name_col.get_view(v) == first_name) {
      try_enqueue_friend(&pq, 1, last_name_col.get_view(v),
                         person_id_col.get_view(v), v);
    }
  }
  const auto root_out = knows_out.get_edges(root);
  for (auto it = root_out.begin(); it != root_out.end(); ++it) {
    const vid_t v = *it;
    d1_friends.push_back(v);
    accessed[v] = true;
    if (first_name_col.get_view(v) == first_name) {
      try_enqueue_friend(&pq, 1, last_name_col.get_view(v),
                         person_id_col.get_view(v), v);
    }
  }

  if (pq.size() == kTopN) {
    results->reserve(pq.size());
    while (!pq.empty()) {
      results->push_back(pq.top());
      pq.pop();
    }
    return;
  }

  for (vid_t u : d1_friends) {
    const auto in_edges = knows_in.get_edges(u);
    for (auto it = in_edges.begin(); it != in_edges.end(); ++it) {
      const vid_t v = *it;
      if (accessed[v]) {
        continue;
      }
      d2_friends.push_back(v);
      accessed[v] = true;
      if (first_name_col.get_view(v) == first_name) {
        try_enqueue_friend(
            &pq, 2, last_name_col.get_view(v),
            person_id_col.get_view(v), v);
      }
    }
    const auto out_edges = knows_out.get_edges(u);
    for (auto it = out_edges.begin(); it != out_edges.end(); ++it) {
      const vid_t v = *it;
      if (accessed[v]) {
        continue;
      }
      d2_friends.push_back(v);
      accessed[v] = true;
      if (first_name_col.get_view(v) == first_name) {
        try_enqueue_friend(
            &pq, 2, last_name_col.get_view(v),
            person_id_col.get_view(v), v);
      }
    }
  }

  if (pq.size() == kTopN) {
    results->reserve(pq.size());
    while (!pq.empty()) {
      results->push_back(pq.top());
      pq.pop();
    }
    return;
  }

  for (vid_t u : d2_friends) {
    const auto in_edges = knows_in.get_edges(u);
    for (auto it = in_edges.begin(); it != in_edges.end(); ++it) {
      const vid_t v = *it;
      if (accessed[v]) {
        continue;
      }
      accessed[v] = true;
      if (first_name_col.get_view(v) == first_name) {
        try_enqueue_friend(
            &pq, 3, last_name_col.get_view(v),
            person_id_col.get_view(v), v);
      }
    }
    const auto out_edges = knows_out.get_edges(u);
    for (auto it = out_edges.begin(); it != out_edges.end(); ++it) {
      const vid_t v = *it;
      if (accessed[v]) {
        continue;
      }
      accessed[v] = true;
      if (first_name_col.get_view(v) == first_name) {
        try_enqueue_friend(
            &pq, 3, last_name_col.get_view(v),
            person_id_col.get_view(v), v);
      }
    }
  }

  results->reserve(pq.size());
  while (!pq.empty()) {
    results->push_back(pq.top());
    pq.pop();
  }
}

std::unique_ptr<function::CallFuncInputBase> bind_ic1(
    const Schema& /*schema*/, const execution::ContextMeta& /*ctx_meta*/,
    const ::physical::PhysicalPlan& plan, int op_idx) {
  const auto& params =
      plan.plan(op_idx).opr().procedure_call().query().arguments();
  if (params.size() < 2) {
    THROW_INVALID_ARGUMENT_EXCEPTION(
        "ic1 requires 2 arguments: personId and firstName");
  }
  auto input = std::make_unique<IC1FuncInput>();
  ldbc::bind_ldbc_call(plan, op_idx, input.get());
  return input;
}

execution::Context exec_ic1(const function::CallFuncInputBase& input,
                            IStorageInterface& graph_iface,
                            const execution::ParamsMap& params) {
  const auto& args = dynamic_cast<const IC1FuncInput&>(input);
  const int64_t person_id = params.at("personId").GetValue<int64_t>();
  const std::string first_name = params.at("firstName").GetValue<std::string>();
  const auto& graph = dynamic_cast<const StorageReadInterface&>(graph_iface);
  const auto& schema = graph.schema();

  const label_t person_label = schema.get_vertex_label_id("PERSON");
  const label_t place_label = schema.get_vertex_label_id("PLACE");
  const label_t org_label = schema.get_vertex_label_id("ORGANISATION");
  const label_t knows_label = schema.get_edge_label_id("KNOWS");
  const label_t is_located_in_label = schema.get_edge_label_id("ISLOCATEDIN");
  const label_t work_at_label = schema.get_edge_label_id("WORKAT");
  const label_t study_at_label = schema.get_edge_label_id("STUDYAT");

  auto first_name_col = ldbc::get_vertex_column<std::string_view>(
      graph, person_label, "firstName");
  auto last_name_col = ldbc::get_vertex_column<std::string_view>(
      graph, person_label, "lastName");
  auto person_id_col = ldbc::get_vertex_column<int64_t>(graph, person_label, "id");
  auto birthday_col =
      ldbc::get_vertex_column<Date>(graph, person_label, "birthday");
  auto creation_date_col =
      ldbc::get_vertex_column<DateTime>(graph, person_label, "creationDate");
  auto gender_col =
      ldbc::get_vertex_column<std::string_view>(graph, person_label, "gender");
  auto browser_used_col = ldbc::get_vertex_column<std::string_view>(
      graph, person_label, "browserUsed");
  auto location_ip_col = ldbc::get_vertex_column<std::string_view>(
      graph, person_label, "locationIP");
  auto email_col =
      ldbc::get_vertex_column<std::string_view>(graph, person_label, "email");
  auto language_col = ldbc::get_vertex_column<std::string_view>(
      graph, person_label, "language");
  auto place_name_col =
      ldbc::get_vertex_column<std::string_view>(graph, place_label, "name");
  auto org_name_col =
      ldbc::get_vertex_column<std::string_view>(graph, org_label, "name");


  vid_t root = StorageReadInterface::kInvalidVid;
  if (!graph.GetVertexIndex(person_label, execution::Value::INT64(person_id),
                            root)) {
    return execution::Context{};
  }

  std::vector<PersonResult> results;
  collect_friends(graph, person_label, knows_label, root, first_name,
                  *first_name_col, *last_name_col, *person_id_col, &results);
  if (results.empty()) {
    return execution::Context{};
  }

  const auto person_located_in_out = graph.GetGenericOutgoingGraphView(
      person_label, place_label, is_located_in_label);
  const auto person_study_at_out = graph.GetGenericOutgoingGraphView(
      person_label, org_label, study_at_label);
  const auto person_work_at_out =
      graph.GetGenericOutgoingGraphView(person_label, org_label, work_at_label);
  const auto org_located_in_out = graph.GetGenericOutgoingGraphView(
      org_label, place_label, is_located_in_label);

  const bool has_class_year = schema.edge_has_property(
      person_label, org_label, study_at_label, "classYear");
  const bool has_work_from = schema.edge_has_property(
      person_label, org_label, work_at_label, "workFrom");
  EdgeDataAccessor class_year_accessor;
  EdgeDataAccessor work_from_accessor;
  if (has_class_year) {
    class_year_accessor = graph.GetEdgeDataAccessor(
        person_label, org_label, study_at_label, "classYear");
  }
  if (has_work_from) {
    work_from_accessor = graph.GetEdgeDataAccessor(person_label, org_label,
                                                   work_at_label, "workFrom");
  }

  execution::ValueColumnBuilder<int64_t> friend_id_builder;
  execution::ValueColumnBuilder<int32_t> distance_builder;
  execution::ValueColumnBuilder<std::string> last_name_builder;
  execution::ValueColumnBuilder<Date> birthday_builder;
  execution::ValueColumnBuilder<DateTime> creation_date_builder;
  execution::ValueColumnBuilder<std::string> gender_builder;
  execution::ValueColumnBuilder<std::string> browser_used_builder;
  execution::ValueColumnBuilder<std::string> location_ip_builder;
  execution::ValueColumnBuilder<std::string> city_name_builder;
  execution::ValueColumnBuilder<std::string> email_builder;
  execution::ValueColumnBuilder<std::string> language_builder;
  execution::ListColumnBuilder universities_builder(kOrgPlaceTupleType);
  execution::ListColumnBuilder companies_builder(kOrgPlaceTupleType);

  for (size_t i = results.size(); i > 0; --i) {
    const auto& row = results[i - 1];
    const vid_t v = row.vid;

    friend_id_builder.push_back_opt(row.id);
    distance_builder.push_back_opt(static_cast<int32_t>(row.distance));
    last_name_builder.push_back_opt(std::string(row.last_name));

    birthday_builder.push_back_opt(birthday_col->get_view(v));
    if (creation_date_col) {
      creation_date_builder.push_back_opt(creation_date_col->get_view(v));
    } else {
      creation_date_builder.push_back_null();
    }
    gender_builder.push_back_opt(std::string(gender_col->get_view(v)));
    if (browser_used_col) {
      browser_used_builder.push_back_opt(
          std::string(browser_used_col->get_view(v)));
    } else {
      browser_used_builder.push_back_null();
    }
    if (location_ip_col) {
      location_ip_builder.push_back_opt(
          std::string(location_ip_col->get_view(v)));
    } else {
      location_ip_builder.push_back_null();
    }

    const vid_t city_vid =
        ldbc::get_single_out_neighbor(person_located_in_out, v);
    if (city_vid != StorageReadInterface::kInvalidVid) {
      city_name_builder.push_back_opt(
          std::string(place_name_col->get_view(city_vid)));
    } else {
      city_name_builder.push_back_null();
    }

    if (email_col) {
      email_builder.push_back_opt(std::string(email_col->get_view(v)));
    } else {
      email_builder.push_back_null();
    }
    if (language_col) {
      language_builder.push_back_opt(std::string(language_col->get_view(v)));
    } else {
      language_builder.push_back_null();
    }

    std::vector<execution::Value> university_values;
    const auto study_edges = person_study_at_out.get_edges(v);
    for (auto it = study_edges.begin(); it != study_edges.end(); ++it) {
      const vid_t org_vid = *it;
      int32_t class_year = 0;
      if (has_class_year) {
        class_year = class_year_accessor.get_typed_data<int32_t>(it);
      }
      const vid_t place_vid =
          ldbc::get_single_out_neighbor(org_located_in_out, org_vid);
      std::string city_name;
      if (place_vid != StorageReadInterface::kInvalidVid) {
        city_name = std::string(place_name_col->get_view(place_vid));
      }
      std::vector<execution::Value> tuple_vals;
      tuple_vals.emplace_back(execution::Value::STRING(
          std::string(org_name_col->get_view(org_vid))));
      tuple_vals.emplace_back(execution::Value::INT32(class_year));
      tuple_vals.emplace_back(execution::Value::STRING(std::move(city_name)));
      university_values.emplace_back(
          execution::Value::STRUCT(kOrgPlaceTupleType, std::move(tuple_vals)));
    }
    universities_builder.push_back_elem(execution::Value::LIST(
        kOrgPlaceTupleType, std::move(university_values)));

    std::vector<execution::Value> company_values;
    const auto work_edges = person_work_at_out.get_edges(v);
    for (auto it = work_edges.begin(); it != work_edges.end(); ++it) {
      const vid_t org_vid = *it;
      int32_t work_from = 0;
      if (has_work_from) {
        work_from = work_from_accessor.get_typed_data<int32_t>(it);
      }
      const vid_t place_vid =
          ldbc::get_single_out_neighbor(org_located_in_out, org_vid);
      std::string country_name;
      if (place_vid != StorageReadInterface::kInvalidVid) {
        country_name = std::string(place_name_col->get_view(place_vid));
      }
      std::vector<execution::Value> tuple_vals;
      tuple_vals.emplace_back(execution::Value::STRING(
          std::string(org_name_col->get_view(org_vid))));
      tuple_vals.emplace_back(execution::Value::INT32(work_from));
      tuple_vals.emplace_back(
          execution::Value::STRING(std::move(country_name)));
      company_values.emplace_back(
          execution::Value::STRUCT(kOrgPlaceTupleType, std::move(tuple_vals)));
    }
    companies_builder.push_back_elem(
        execution::Value::LIST(kOrgPlaceTupleType, std::move(company_values)));
  }

  return ldbc::make_output_context(
      args.output_aliases,
      {friend_id_builder.finish(), distance_builder.finish(),
       last_name_builder.finish(), birthday_builder.finish(),
       creation_date_builder.finish(), gender_builder.finish(),
       browser_used_builder.finish(), location_ip_builder.finish(),
       city_name_builder.finish(), email_builder.finish(),
       language_builder.finish(), universities_builder.finish(),
       companies_builder.finish()});
}

}  // namespace

function::function_set IC1Function::getFunctionSet() {
  const auto kOrgPlaceListType =
      common::DataType::List(kOrgPlaceTupleCatalogType);
  auto fn = std::make_unique<function::NeugCallFunction>(
      IC1Function::name,
      std::vector<common::DataTypeId>{common::DataTypeId::kInt64,
                                      common::DataTypeId::kVarchar},
      function::call_output_columns{
          {"friendId", common::DataType(common::DataTypeId::kInt64)},
          {"distanceFromPerson", common::DataType(common::DataTypeId::kInt32)},
          {"friendLastName", common::DataType(common::DataTypeId::kVarchar)},
          {"friendBirthday", common::DataType(common::DataTypeId::kDate)},
          {"friendCreationDate", common::DataType(common::DataTypeId::kTimestampMs)},
          {"friendGender", common::DataType(common::DataTypeId::kVarchar)},
          {"friendBrowserUsed", common::DataType(common::DataTypeId::kVarchar)},
          {"friendLocationIp", common::DataType(common::DataTypeId::kVarchar)},
          {"friendCityName", common::DataType(common::DataTypeId::kVarchar)},
          {"friendEmail", common::DataType(common::DataTypeId::kVarchar)},
          {"friendLanguage", common::DataType(common::DataTypeId::kVarchar)},
          {"friendUniversities", kOrgPlaceListType},
          {"friendCompanies", kOrgPlaceListType}});
  fn->bindFunc = bind_ic1;
  fn->execFunc = exec_ic1;
  function::function_set set;
  set.push_back(std::move(fn));
  return set;
}

}  // namespace ldbc_ic
}  // namespace extension
}  // namespace neug
