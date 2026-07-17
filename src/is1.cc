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

#include "is1.h"

#include <string>

#include "ldbc_common.h"
#include "neug/common/columns/value_columns.h"
#include "neug/execution/common/context_chunk.h"
#include "neug/utils/exception/exception.h"

namespace neug {
namespace extension {
namespace ldbc {
class IS1 {
 public:
  static std::unique_ptr<function::CallFuncInputBase> bind(
      const Schema& /*schema*/, const execution::ContextMeta& /*ctx_meta*/,
      const ::physical::PhysicalPlan& plan, int op_idx) {
    auto input = std::make_unique<IS1FuncInput>();
    ldbc::bind_ldbc_call(plan, op_idx, input.get());
    return input;
  }

  static execution::Context exec(const function::CallFuncInputBase& input,
                                 IStorageInterface& graph_iface) {
    const auto& is1_input = dynamic_cast<const IS1FuncInput&>(input);
    const int64_t person_id = is1_input.person_id;
    const auto& graph = dynamic_cast<const StorageReadInterface&>(graph_iface);
    const auto& schema = graph.schema();

    const label_t person_label = schema.get_vertex_label_id("PERSON");
    const label_t place_label = schema.get_vertex_label_id("PLACE");
    const label_t is_located_in_label = schema.get_edge_label_id("ISLOCATEDIN");

    auto first_name_col = ldbc::get_vertex_column<std::string_view>(
        graph, person_label, "firstName");
    auto last_name_col = ldbc::get_vertex_column<std::string_view>(
        graph, person_label, "lastName");
    auto birthday_col =
        ldbc::get_vertex_column<Date>(graph, person_label, "birthday");
    auto location_ip_col = ldbc::get_vertex_column<std::string_view>(
        graph, person_label, "locationIP");
    auto browser_used_col = ldbc::get_vertex_column<std::string_view>(
        graph, person_label, "browserUsed");
    auto gender_col = ldbc::get_vertex_column<std::string_view>(
        graph, person_label, "gender");
    auto creation_date_col =
        ldbc::get_vertex_column<DateTime>(graph, person_label, "creationDate");
    auto place_id_col =
        ldbc::get_vertex_column<int64_t>(graph, place_label, "id");

    vid_t person_vid = StorageReadInterface::kInvalidVid;
    if (!graph.GetVertexIndex(person_label, Value::INT64(person_id),
                              person_vid)) {
      return execution::Context{};
    }

    const auto located_in_out = graph.GetGenericOutgoingGraphView(
        person_label, place_label, is_located_in_label);
    const vid_t place_vid =
        ldbc::get_single_out_neighbor(located_in_out, person_vid);
    int64_t city_id = 0;
    if (place_vid != StorageReadInterface::kInvalidVid) {
      city_id = place_id_col->get_view(place_vid);
    }

    ValueColumnBuilder<std::string> first_name_builder;
    ValueColumnBuilder<std::string> last_name_builder;
    ValueColumnBuilder<Date> birthday_builder;
    ValueColumnBuilder<std::string> location_ip_builder;
    ValueColumnBuilder<std::string> browser_used_builder;
    ValueColumnBuilder<int64_t> city_id_builder;
    ValueColumnBuilder<std::string> gender_builder;
    ValueColumnBuilder<DateTime> creation_date_builder;

    first_name_builder.push_back_opt(
        std::string(first_name_col->get_view(person_vid)));
    last_name_builder.push_back_opt(
        std::string(last_name_col->get_view(person_vid)));
    birthday_builder.push_back_opt(birthday_col->get_view(person_vid));
    location_ip_builder.push_back_opt(
        std::string(location_ip_col->get_view(person_vid)));
    browser_used_builder.push_back_opt(
        std::string(browser_used_col->get_view(person_vid)));
    city_id_builder.push_back_opt(city_id);
    gender_builder.push_back_opt(std::string(gender_col->get_view(person_vid)));
    creation_date_builder.push_back_opt(
        creation_date_col->get_view(person_vid));

    execution::ContextChunk chunk;
    chunk.set(0, first_name_builder.finish());
    chunk.set(1, last_name_builder.finish());
    chunk.set(2, birthday_builder.finish());
    chunk.set(3, location_ip_builder.finish());
    chunk.set(4, browser_used_builder.finish());
    chunk.set(5, city_id_builder.finish());
    chunk.set(6, gender_builder.finish());
    chunk.set(7, creation_date_builder.finish());
    execution::Context ctx;
    ctx.append_chunk(std::move(chunk));
    ctx.tag_ids = is1_input.output_aliases;
    return ctx;
  }
};

function::function_set IS1Function::getFunctionSet() {
  auto function = std::make_unique<function::NeugCallFunction>(
      IS1Function::name,
      function::call_input_types{common::DataType(common::DataTypeId::kInt64)},
      function::call_output_columns{
          {"firstName", common::DataType(common::DataTypeId::kVarchar)},
          {"lastName", common::DataType(common::DataTypeId::kVarchar)},
          {"birthday", common::DataType(common::DataTypeId::kDate)},
          {"locationIp", common::DataType(common::DataTypeId::kVarchar)},
          {"browserUsed", common::DataType(common::DataTypeId::kVarchar)},
          {"cityId", common::DataType(common::DataTypeId::kInt64)},
          {"gender", common::DataType(common::DataTypeId::kVarchar)},
          {"creationDate",
           common::DataType(common::DataTypeId::kTimestampMs)}});
  function->bindFunc = IS1::bind;
  function->execFunc = IS1::exec;
  function::function_set function_set;
  function_set.push_back(std::move(function));
  return function_set;
}

}  // namespace ldbc
}  // namespace extension
}  // namespace neug
