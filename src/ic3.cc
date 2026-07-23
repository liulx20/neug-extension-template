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

#include "ic3.h"

#include <queue>
#include <string>
#include <vector>

#include "ldbc_common.h"
#include "neug/common/columns/value_columns.h"
#include "neug/compiler/common/types/types.h"
#include "neug/execution/common/context.h"
#include "neug/execution/common/context_chunk.h"
#include "neug/utils/exception/exception.h"

namespace neug {
namespace extension {
namespace ldbc {

class IC3 {
 public:
  static constexpr size_t kTopN = 20;
  static constexpr int64_t kMillisPerDay = 24L * 60 * 60 * 1000;

  struct PersonResult {
    int total = 0;
    vid_t person_vid = 0;
    int64_t person_id = 0;
    int count_x = 0;
    int count_y = 0;
  };

  struct PersonResultComparer {
    bool operator()(const PersonResult& lhs, const PersonResult& rhs) const {
      if (lhs.total > rhs.total) {
        return true;
      }
      if (lhs.total < rhs.total) {
        return false;
      }
      return lhs.person_id < rhs.person_id;
    }
  };

  static std::unique_ptr<function::CallFuncInputBase> bind(
      const Schema& /*schema*/, const execution::ContextMeta& /*ctx_meta*/,
      const ::physical::PhysicalPlan& plan, int op_idx) {
    const auto& procedure_call = plan.plan(op_idx).opr().procedure_call();
    const auto& params = procedure_call.query().arguments();
    if (params.size() < 5) {
      THROW_INVALID_ARGUMENT_EXCEPTION(
          "ic3 requires 5 arguments: personId, countryXName, countryYName, "
          "startDate, durationDays");
    }

    auto input = std::make_unique<IC3FuncInput>();
    ldbc::bind_ldbc_call(plan, op_idx, *input);
    return input;
  }

  static execution::Context exec(const function::CallFuncInputBase& input,
                                 IStorageInterface& graph_iface) {
    const auto& ic3_input = dynamic_cast<const IC3FuncInput&>(input);
    const int64_t person_id = ic3_input.person_id;
    const std::string country_x_name = ic3_input.country_x_name;
    const std::string country_y_name = ic3_input.country_y_name;
    const int64_t start_date_ms = ic3_input.start_date_ms;
    const int64_t duration_days = ic3_input.duration_days;
    const auto& graph = dynamic_cast<const StorageReadInterface&>(graph_iface);
    const auto& schema = graph.schema();

    const label_t person_label = schema.get_vertex_label_id("PERSON");
    const label_t post_label = schema.get_vertex_label_id("POST");
    const label_t comment_label = schema.get_vertex_label_id("COMMENT");
    const label_t place_label = schema.get_vertex_label_id("PLACE");
    const label_t knows_label = schema.get_edge_label_id("KNOWS");
    const label_t has_creator_label = schema.get_edge_label_id("HASCREATOR");
    const label_t is_located_in_label = schema.get_edge_label_id("ISLOCATEDIN");
    const label_t is_part_of_label = schema.get_edge_label_id("ISPARTOF");

    auto first_name_col = ldbc::get_vertex_column<std::string_view>(
        graph, person_label, "firstName");
    auto last_name_col = ldbc::get_vertex_column<std::string_view>(
        graph, person_label, "lastName");
    auto person_id_col =
        ldbc::get_vertex_column<int64_t>(graph, person_label, "id");
    auto place_name_col =
        ldbc::get_vertex_column<std::string_view>(graph, place_label, "name");
    if (!first_name_col || !last_name_col || !place_name_col ||
        !person_id_col) {
      THROW_RUNTIME_ERROR("ic3: failed to load required LDBC property columns");
    }

    vid_t root = StorageReadInterface::kInvalidVid;
    if (!graph.GetVertexIndex(person_label, Value::INT64(person_id), root)) {
      return execution::Context{};
    }

    const auto place_set = graph.GetVertexSet(place_label);
    vid_t country_x = StorageReadInterface::kInvalidVid;
    vid_t country_y = StorageReadInterface::kInvalidVid;
    for (auto it = place_set.begin(); it != place_set.end(); ++it) {
      const vid_t place_vid = *it;
      const auto name = place_name_col->get_view(place_vid);
      if (name == country_x_name) {
        country_x = place_vid;
      }
      if (name == country_y_name) {
        country_y = place_vid;
      }
    }
    if (country_x == StorageReadInterface::kInvalidVid ||
        country_y == StorageReadInterface::kInvalidVid) {
      THROW_INVALID_ARGUMENT_EXCEPTION("ic3: country not found");
    }

    std::vector<uint8_t> city_in_country_x_or_y(place_set.size(), 0);

    const auto city_in_country = graph.GetGenericIncomingGraphView(
        place_label, place_label, is_part_of_label);
    const auto& city_in_country_x = city_in_country.get_edges(country_x);
    const auto& city_in_country_y = city_in_country.get_edges(country_y);
    for (auto it = city_in_country_x.begin(); it != city_in_country_x.end();
         ++it) {
      city_in_country_x_or_y[*it] = 1;
    }
    for (auto it = city_in_country_y.begin(); it != city_in_country_y.end();
         ++it) {
      city_in_country_x_or_y[*it] = 1;
    }

    const auto person_located_in = graph.GetGenericOutgoingGraphView(
        person_label, place_label, is_located_in_label);
    const auto post_located_in = graph.GetGenericOutgoingGraphView(
        post_label, place_label, is_located_in_label);
    const auto comment_located_in = graph.GetGenericOutgoingGraphView(
        comment_label, place_label, is_located_in_label);

    std::vector<vid_t> friends;
    ldbc::foreach_knows_1d_2d_neighbor(
        graph, person_label, knows_label, root, [&](vid_t friend_vid) {
          const vid_t city_vid =
              ldbc::get_single_out_neighbor(person_located_in, friend_vid);
          if (city_vid == StorageReadInterface::kInvalidVid ||
              city_in_country_x_or_y[city_vid]) {
            return;
          }
          friends.push_back(friend_vid);
        });

    const int64_t end_date_ms = start_date_ms + duration_days * kMillisPerDay;
    const auto post_has_creator_in = ldbc::get_typed_incoming_view(
        graph, person_label, post_label, has_creator_label);
    const auto comment_has_creator_in = ldbc::get_typed_incoming_view(
        graph, person_label, comment_label, has_creator_label);

    std::priority_queue<PersonResult, std::vector<PersonResult>,
                        PersonResultComparer>
        pq;

    for (vid_t friend_vid : friends) {
      int x_count = 0;
      int y_count = 0;

      auto scan_friend_messages = [&](const CsrView& located_in_view,
                                      const ldbc::TypedView& has_creator_in) {
        ldbc::foreach_incoming_nbr_between_half_open(
            has_creator_in, friend_vid, start_date_ms, end_date_ms,
            [&](vid_t message_vid, const DateTime& /*creation_date*/) {
              const vid_t locate =
                  ldbc::get_single_out_neighbor(located_in_view, message_vid);
              if (locate == country_x) {
                ++x_count;
              } else if (locate == country_y) {
                ++y_count;
              }
            });
      };

      scan_friend_messages(post_located_in, post_has_creator_in);
      scan_friend_messages(comment_located_in, comment_has_creator_in);

      if (x_count == 0 || y_count == 0) {
        continue;
      }

      PersonResult row;
      row.person_vid = friend_vid;
      row.count_x = x_count;
      row.count_y = y_count;
      row.total = x_count + y_count;
      row.person_id = person_id_col->get_view(friend_vid);
      if (pq.size() < kTopN) {
        pq.push(row);
        continue;
      }
      const auto& worst = pq.top();
      if (row.total > worst.total) {
        pq.pop();
        pq.push(row);
        continue;
      }
      if (row.total == worst.total && row.person_id < worst.person_id) {
        pq.pop();
        pq.push(row);
      }
    }

    std::vector<PersonResult> results;
    results.reserve(pq.size());
    while (!pq.empty()) {
      results.push_back(pq.top());
      pq.pop();
    }

    ValueColumnBuilder<int64_t> person_id_builder;
    ValueColumnBuilder<std::string> first_name_builder;
    ValueColumnBuilder<std::string> last_name_builder;
    ValueColumnBuilder<int64_t> country_x_builder;
    ValueColumnBuilder<int64_t> country_y_builder;
    ValueColumnBuilder<int64_t> total_builder;

    person_id_builder.reserve(results.size());
    first_name_builder.reserve(results.size());
    last_name_builder.reserve(results.size());
    country_x_builder.reserve(results.size());
    country_y_builder.reserve(results.size());
    total_builder.reserve(results.size());

    for (size_t i = results.size(); i > 0; --i) {
      const auto& row = results[i - 1];
      person_id_builder.push_back_opt(row.person_id);
      first_name_builder.push_back_opt(
          std::string(first_name_col->get_view(row.person_vid)));
      last_name_builder.push_back_opt(
          std::string(last_name_col->get_view(row.person_vid)));
      country_x_builder.push_back_opt(static_cast<int64_t>(row.count_x));
      country_y_builder.push_back_opt(static_cast<int64_t>(row.count_y));
      total_builder.push_back_opt(static_cast<int64_t>(row.total));
    }

    execution::ContextChunk chunk;
    chunk.set(0, person_id_builder.finish());
    chunk.set(1, first_name_builder.finish());
    chunk.set(2, last_name_builder.finish());
    chunk.set(3, country_x_builder.finish());
    chunk.set(4, country_y_builder.finish());
    chunk.set(5, total_builder.finish());
    execution::Context ctx;
    ctx.append_chunk(std::move(chunk));
    ctx.tag_ids = ic3_input.output_aliases;
    return ctx;
  }
};

function::function_set IC3Function::getFunctionSet() {
  auto function = std::make_unique<function::NeugCallFunction>(
      IC3Function::name,
      function::call_input_types{common::DataType(common::DataTypeId::kInt64),
                                 common::DataType(common::DataTypeId::kVarchar),
                                 common::DataType(common::DataTypeId::kVarchar),
                                 common::DataType(common::DataTypeId::kInt64),
                                 common::DataType(common::DataTypeId::kInt64)},
      function::call_output_columns{
          {"personId", common::DataType(common::DataTypeId::kInt64)},
          {"personFirstName", common::DataType(common::DataTypeId::kVarchar)},
          {"personLastName", common::DataType(common::DataTypeId::kVarchar)},
          {"countryXCount", common::DataType(common::DataTypeId::kInt64)},
          {"countryYCount", common::DataType(common::DataTypeId::kInt64)},
          {"totalCount", common::DataType(common::DataTypeId::kInt64)}});

  function->bindFunc = IC3::bind;
  function->execFunc = IC3::exec;

  function::function_set function_set;
  function_set.push_back(std::move(function));
  return function_set;
}

}  // namespace ldbc
}  // namespace extension
}  // namespace neug
