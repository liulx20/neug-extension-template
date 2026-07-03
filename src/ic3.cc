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

#include <glog/logging.h>
#include <array>
#include <queue>
#include <string>
#include <string_view>
#include <vector>

#include "neug/compiler/common/types/types.h"
#include "neug/execution/common/columns/value_columns.h"
#include "neug/execution/common/context.h"
#include "neug/execution/common/context_chunk.h"
#include "neug/execution/common/types/value.h"
#include "ldbc_common.h"
#include "neug/storages/csr/csr_view.h"
#include "neug/utils/exception/exception.h"

namespace neug {
namespace extension {
namespace ldbc_ic {
namespace {

constexpr size_t kTopN = 20;
constexpr size_t kNumOutputColumns = 6;
constexpr int64_t kMillisPerDay = 24L * 60 * 60 * 1000;

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

int64_t parse_i64_arg(const ::common::Value& value, const char* arg_name) {
  if (value.has_i64()) {
    return value.i64();
  }
  if (value.has_i32()) {
    return value.i32();
  }
  THROW_INVALID_ARGUMENT_EXCEPTION(
      std::string("ic3: argument ") + arg_name + " must be an integer");
}

std::string parse_string_arg(const ::common::Value& value,
                             const char* arg_name) {
  if (value.has_str()) {
    return value.str();
  }
  THROW_INVALID_ARGUMENT_EXCEPTION(
      std::string("ic3: argument ") + arg_name + " must be a string");
}

template <typename T>
std::shared_ptr<StorageReadInterface::vertex_column_t<T>> get_vertex_column(
    const StorageReadInterface& graph, label_t label,
    const std::string& prop_name) {
  auto col = graph.GetVertexPropColumn(label, prop_name);
  return std::dynamic_pointer_cast<StorageReadInterface::vertex_column_t<T>>(
      col);
}

size_t count_edges(const CsrView& view, vid_t vertex) {
  size_t count = 0;
  for (auto it = view.get_edges(vertex).begin(); it != view.get_edges(vertex).end();
       ++it) {
    ++count;
  }
  return count;
}

vid_t get_single_out_neighbor(const CsrView& view, vid_t vertex) {
  for (auto it = view.get_edges(vertex).begin(); it != view.get_edges(vertex).end();
       ++it) {
    return *it;
  }
  return StorageReadInterface::kInvalidVid;
}

void consider_person(
    std::priority_queue<PersonResult, std::vector<PersonResult>,
                        PersonResultComparer>& pq,
    const PersonResult& candidate) {
  if (pq.size() < kTopN) {
    pq.push(candidate);
    return;
  }
  const auto& worst = pq.top();
  if (candidate.total > worst.total) {
    pq.pop();
    pq.push(candidate);
    return;
  }
  if (candidate.total == worst.total &&
      candidate.person_id < worst.person_id) {
    pq.pop();
    pq.push(candidate);
  }
}

std::unique_ptr<function::CallFuncInputBase> bind_ic3(
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
  for (size_t i = 0; i < 5; ++i) {
    if (!params[static_cast<int>(i)].has_const_()) {
      THROW_INVALID_ARGUMENT_EXCEPTION("ic3: all arguments must be literals");
    }
  }

  input->person_id = parse_i64_arg(params[0].const_(), "personId");
  input->country_x_name = parse_string_arg(params[1].const_(), "countryXName");
  input->country_y_name = parse_string_arg(params[2].const_(), "countryYName");
  input->start_date_ms = parse_i64_arg(params[3].const_(), "startDate");
  input->duration_days = parse_i64_arg(params[4].const_(), "durationDays");

  const auto& physical_op = plan.plan(op_idx);
  input->output_aliases.reserve(physical_op.meta_data_size());
  for (int i = 0; i < physical_op.meta_data_size(); ++i) {
    input->output_aliases.push_back(physical_op.meta_data(i).alias());
  }
  return input;
}

execution::Context exec_ic3(const function::CallFuncInputBase& input,
                            IStorageInterface& graph_iface) {
  const auto& ic3_input = dynamic_cast<const IC3FuncInput&>(input);
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

  auto first_name_col =
      get_vertex_column<std::string_view>(graph, person_label, "firstName");
  auto last_name_col =
      get_vertex_column<std::string_view>(graph, person_label, "lastName");
  auto place_name_col =
      get_vertex_column<std::string_view>(graph, place_label, "name");
  auto post_creation_date_col =
      get_vertex_column<DateTime>(graph, post_label, "creationDate");
  auto comment_creation_date_col =
      get_vertex_column<DateTime>(graph, comment_label, "creationDate");

  if (!first_name_col || !last_name_col || !place_name_col ||
      !post_creation_date_col || !comment_creation_date_col) {
    THROW_RUNTIME_ERROR("ic3: failed to load required LDBC property columns");
  }

  vid_t root = StorageReadInterface::kInvalidVid;
  if (!graph.GetVertexIndex(person_label,
                            execution::Value::INT64(ic3_input.person_id),
                            root)) {
    return execution::Context{};
  }

  const auto place_set = graph.GetVertexSet(place_label);
  vid_t country_x = StorageReadInterface::kInvalidVid;
  vid_t country_y = StorageReadInterface::kInvalidVid;
  for (auto it = place_set.begin(); it != place_set.end(); ++it) {
    const vid_t place_vid = *it;
    const auto name = place_name_col->get_view(place_vid);
    if (name == ic3_input.country_x_name) {
      country_x = place_vid;
    }
    if (name == ic3_input.country_y_name) {
      country_y = place_vid;
    }
  }
  if (country_x == StorageReadInterface::kInvalidVid ||
      country_y == StorageReadInterface::kInvalidVid) {
    THROW_INVALID_ARGUMENT_EXCEPTION("ic3: country not found");
  }

  const auto person_set = graph.GetVertexSet(person_label);
  std::vector<bool> city_in_country_x_or_y(place_set.size(), false);

  const auto city_in_country = graph.GetGenericIncomingGraphView(
      place_label, place_label, is_part_of_label);
  for (auto it = city_in_country.get_edges(country_x).begin();
       it != city_in_country.get_edges(country_x).end(); ++it) {
    const vid_t city_vid = *it;
    if (city_vid < city_in_country_x_or_y.size()) {
      city_in_country_x_or_y[city_vid] = true;
    }
  }
  for (auto it = city_in_country.get_edges(country_y).begin();
       it != city_in_country.get_edges(country_y).end(); ++it) {
    const vid_t city_vid = *it;
    if (city_vid < city_in_country_x_or_y.size()) {
      city_in_country_x_or_y[city_vid] = true;
    }
  }

  const auto person_located_in = graph.GetGenericOutgoingGraphView(
      person_label, place_label, is_located_in_label);
  const auto post_has_creator = graph.GetGenericOutgoingGraphView(
      post_label, person_label, has_creator_label);
  const auto comment_has_creator = graph.GetGenericOutgoingGraphView(
      comment_label, person_label, has_creator_label);
  const auto post_located_in = graph.GetGenericOutgoingGraphView(
      post_label, place_label, is_located_in_label);
  const auto comment_located_in = graph.GetGenericOutgoingGraphView(
      comment_label, place_label, is_located_in_label);
  const auto post_in_country = graph.GetGenericIncomingGraphView(
      place_label, post_label, is_located_in_label);
  const auto comment_in_country = graph.GetGenericIncomingGraphView(
      place_label, comment_label, is_located_in_label);
  const auto post_creator_in = graph.GetGenericIncomingGraphView(
      person_label, post_label, has_creator_label);
  const auto comment_creator_in = graph.GetGenericIncomingGraphView(
      person_label, comment_label, has_creator_label);
  const bool post_has_creator_date = schema.edge_has_property(
      post_label, person_label, has_creator_label, "creationDate");
  const bool comment_has_creator_date = schema.edge_has_property(
      comment_label, person_label, has_creator_label, "creationDate");
  EdgeDataAccessor post_creator_accessor;
  EdgeDataAccessor comment_creator_accessor;
  if (post_has_creator_date) {
    post_creator_accessor = graph.GetEdgeDataAccessor(
        post_label, person_label, has_creator_label, "creationDate");
  }
  if (comment_has_creator_date) {
    comment_creator_accessor = graph.GetEdgeDataAccessor(
        comment_label, person_label, has_creator_label, "creationDate");
  }

  std::vector<bool> is_friend(person_set.size(), false);
  std::vector<vid_t> friends;
  size_t friend_messages_num = 0;
  ldbc::foreach_knows_1d_2d_neighbor(
      graph, person_label, knows_label, root, [&](vid_t friend_vid) {
        const vid_t city_vid =
            get_single_out_neighbor(person_located_in, friend_vid);
        if (city_vid != StorageReadInterface::kInvalidVid &&
            city_vid < city_in_country_x_or_y.size() &&
            city_in_country_x_or_y[city_vid]) {
          return;
        }
        if (friend_vid < is_friend.size()) {
          is_friend[friend_vid] = true;
        }
        friends.push_back(friend_vid);
        friend_messages_num += count_edges(post_creator_in, friend_vid);
        friend_messages_num += count_edges(comment_creator_in, friend_vid);
      });

  const size_t country_xy_messages_num =
      count_edges(post_in_country, country_x) +
      count_edges(comment_in_country, country_x) +
      count_edges(post_in_country, country_y) +
      count_edges(comment_in_country, country_y);

  const int64_t end_date_ms =
      ic3_input.start_date_ms + ic3_input.duration_days * kMillisPerDay;

  auto in_date_range = [&](int64_t creation_ms) {
    return creation_ms >= ic3_input.start_date_ms &&
           creation_ms < end_date_ms;
  };

  std::priority_queue<PersonResult, std::vector<PersonResult>,
                      PersonResultComparer>
      pq;

  if (friend_messages_num > country_xy_messages_num) {
    std::vector<std::pair<int, int>> counts(person_set.size(), {0, 0});

    auto scan_country_messages = [&](vid_t country_vid, bool is_x,
                                     label_t message_label,
                                     const CsrView& message_in_country,
                                     const CsrView& message_has_creator,
                                     bool has_creator_date,
                                     const EdgeDataAccessor& creator_accessor,
                                     const StorageReadInterface::vertex_column_t<
                                         DateTime>* message_date_col) {
      for (auto it = message_in_country.get_edges(country_vid).begin();
           it != message_in_country.get_edges(country_vid).end(); ++it) {
        const vid_t message_vid = *it;
        for (auto cit = message_has_creator.get_edges(message_vid).begin();
             cit != message_has_creator.get_edges(message_vid).end(); ++cit) {
          int64_t creation_ms = 0;
          if (has_creator_date) {
            creation_ms =
                creator_accessor.get_typed_data<DateTime>(cit).milli_second;
          } else if (message_date_col) {
            creation_ms = message_date_col->get_view(message_vid).milli_second;
          }
          if (!in_date_range(creation_ms)) {
            continue;
          }
          const vid_t person_vid = *cit;
          if (person_vid < is_friend.size() && is_friend[person_vid]) {
            if (is_x) {
              counts[person_vid].first += 1;
            } else {
              counts[person_vid].second += 1;
            }
          }
        }
      }
    };

    scan_country_messages(country_x, true, post_label, post_in_country,
                          post_has_creator, post_has_creator_date,
                          post_creator_accessor, post_creation_date_col.get());
    scan_country_messages(country_x, true, comment_label, comment_in_country,
                          comment_has_creator, comment_has_creator_date,
                          comment_creator_accessor,
                          comment_creation_date_col.get());
    scan_country_messages(country_y, false, post_label, post_in_country,
                          post_has_creator, post_has_creator_date,
                          post_creator_accessor, post_creation_date_col.get());
    scan_country_messages(country_y, false, comment_label, comment_in_country,
                          comment_has_creator, comment_has_creator_date,
                          comment_creator_accessor,
                          comment_creation_date_col.get());

    for (vid_t friend_vid : friends) {
      const auto& count = counts[friend_vid];
      if (count.first == 0 || count.second == 0) {
        continue;
      }
      PersonResult row;
      row.person_vid = friend_vid;
      row.person_id =
          graph.GetVertexId(person_label, friend_vid).GetValue<int64_t>();
      row.count_x = count.first;
      row.count_y = count.second;
      row.total = count.first + count.second;
      consider_person(pq, row);
    }
  } else {
    for (vid_t friend_vid : friends) {
      int x_count = 0;
      int y_count = 0;

      auto scan_friend_messages = [&](label_t message_label,
                                      const CsrView& located_in_view) {
        ldbc::foreach_incoming_nbr_between_half_open(
            graph, person_label, message_label, has_creator_label, friend_vid,
            ic3_input.start_date_ms, end_date_ms,
            [&](vid_t message_vid, const DateTime& /*creation_date*/) {
              const vid_t locate =
                  get_single_out_neighbor(located_in_view, message_vid);
              if (locate == country_x) {
                ++x_count;
              } else if (locate == country_y) {
                ++y_count;
              }
            });
      };

      scan_friend_messages(post_label, post_located_in);
      scan_friend_messages(comment_label, comment_located_in);

      if (x_count == 0 || y_count == 0) {
        continue;
      }
      PersonResult row;
      row.person_vid = friend_vid;
      row.person_id =
          graph.GetVertexId(person_label, friend_vid).GetValue<int64_t>();
      row.count_x = x_count;
      row.count_y = y_count;
      row.total = x_count + y_count;
      consider_person(pq, row);
    }
  }

  std::vector<PersonResult> results;
  results.reserve(pq.size());
  while (!pq.empty()) {
    results.push_back(pq.top());
    pq.pop();
  }

  execution::ValueColumnBuilder<int64_t> person_id_builder;
  execution::ValueColumnBuilder<std::string> first_name_builder;
  execution::ValueColumnBuilder<std::string> last_name_builder;
  execution::ValueColumnBuilder<int64_t> country_x_builder;
  execution::ValueColumnBuilder<int64_t> country_y_builder;
  execution::ValueColumnBuilder<int64_t> total_builder;

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

  std::array<std::shared_ptr<execution::IContextColumn>, kNumOutputColumns>
      output_columns;
  output_columns[0] = person_id_builder.finish();
  output_columns[1] = first_name_builder.finish();
  output_columns[2] = last_name_builder.finish();
  output_columns[3] = country_x_builder.finish();
  output_columns[4] = country_y_builder.finish();
  output_columns[5] = total_builder.finish();

  execution::Context ctx;
  execution::ContextChunk out_chunk;
  ctx.tag_ids = ic3_input.output_aliases;
  for (size_t i = 0; i < ic3_input.output_aliases.size(); ++i) {
    out_chunk.set(ic3_input.output_aliases[i], output_columns[i]);
  }
  ctx.append_chunk(std::move(out_chunk));
  return ctx;
}

}  // namespace

function::function_set IC3Function::getFunctionSet() {
  auto function = std::make_unique<function::NeugCallFunction>(
      IC3Function::name,
      std::vector<common::DataTypeId>{
          common::DataTypeId::kInt64, common::DataTypeId::kVarchar,
          common::DataTypeId::kVarchar, common::DataTypeId::kInt64,
          common::DataTypeId::kInt64},
      std::vector<std::pair<std::string, common::DataTypeId>>{
          {"personId", common::DataTypeId::kInt64},
          {"personFirstName", common::DataTypeId::kVarchar},
          {"personLastName", common::DataTypeId::kVarchar},
          {"countryXCount", common::DataTypeId::kInt64},
          {"countryYCount", common::DataTypeId::kInt64},
          {"totalCount", common::DataTypeId::kInt64}});

  function->bindFunc = bind_ic3;
  function->execFunc = exec_ic3;

  function::function_set function_set;
  function_set.push_back(std::move(function));
  return function_set;
}

}  // namespace ldbc_ic
}  // namespace extension
}  // namespace neug
