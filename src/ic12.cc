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

#include "ic12.h"

#include <queue>
#include <set>
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
  int reply_count = 0;
  vid_t person_vid = 0;
  int64_t person_id = 0;
};

struct PersonResultComparer {
  bool operator()(const PersonResult& lhs, const PersonResult& rhs) const {
    if (lhs.reply_count > rhs.reply_count) {
      return true;
    }
    if (lhs.reply_count < rhs.reply_count) {
      return false;
    }
    return lhs.person_id < rhs.person_id;
  }
};

void collect_tags_in_class(const StorageReadInterface& graph,
                           label_t tag_class_label, label_t tag_label,
                           label_t has_type_label, label_t is_subclass_of_label,
                           vid_t tag_class_vid, std::vector<uint8_t>* tag_set) {
  tag_set->assign(graph.GetVertexSet(tag_label).size(), 0);
  const auto subclass_in = graph.GetGenericIncomingGraphView(
      tag_class_label, tag_class_label, is_subclass_of_label);
  const auto tag_has_type_in = graph.GetGenericIncomingGraphView(
      tag_class_label, tag_label, has_type_label);

  std::queue<vid_t> q;
  q.push(tag_class_vid);
  while (!q.empty()) {
    const vid_t current = q.front();
    q.pop();
    const auto subclasses = subclass_in.get_edges(current);
    for (auto it = subclasses.begin(); it != subclasses.end(); ++it) {
      q.push(*it);
    }
    const auto tags = tag_has_type_in.get_edges(current);
    for (auto it = tags.begin(); it != tags.end(); ++it) {
      (*tag_set)[*it] = 1;
    }
  }
}

std::unique_ptr<function::CallFuncInputBase> bind_ic12(
    const Schema& /*schema*/, const execution::ContextMeta& /*ctx_meta*/,
    const ::physical::PhysicalPlan& plan, int op_idx) {
  auto input = std::make_unique<IC12FuncInput>();
  ldbc::bind_ldbc_call(plan, op_idx, input.get());
  return input;
}

execution::Context exec_ic12(const function::CallFuncInputBase& input,
                             IStorageInterface& graph_iface,
                             const execution::ParamsMap& params) {
  const auto& args = dynamic_cast<const IC12FuncInput&>(input);
  const int64_t person_id = params.at("personId").GetValue<int64_t>();
  const std::string tag_class_name =
      params.at("tagClassName").GetValue<std::string>();
  const auto& graph = dynamic_cast<const StorageReadInterface&>(graph_iface);
  const auto& schema = graph.schema();

  const label_t person_label = schema.get_vertex_label_id("PERSON");
  const label_t tag_label = schema.get_vertex_label_id("TAG");
  const label_t tag_class_label = schema.get_vertex_label_id("TAGCLASS");
  const label_t post_label = schema.get_vertex_label_id("POST");
  const label_t comment_label = schema.get_vertex_label_id("COMMENT");
  const label_t knows_label = schema.get_edge_label_id("KNOWS");
  const label_t has_creator_label = schema.get_edge_label_id("HASCREATOR");
  const label_t has_tag_label = schema.get_edge_label_id("HASTAG");
  const label_t has_type_label = schema.get_edge_label_id("HASTYPE");
  const label_t is_subclass_of_label = schema.get_edge_label_id("ISSUBCLASSOF");
  const label_t reply_of_label = schema.get_edge_label_id("REPLYOF");

  auto first_name_col = ldbc::get_vertex_column<std::string_view>(
      graph, person_label, "firstName");
  auto last_name_col = ldbc::get_vertex_column<std::string_view>(
      graph, person_label, "lastName");
  auto tag_name_col =
      ldbc::get_vertex_column<std::string_view>(graph, tag_label, "name");
  auto person_id_col = ldbc::get_vertex_column<int64_t>(graph, person_label, "id");
  if (!first_name_col || !last_name_col || !tag_name_col) {
    THROW_RUNTIME_ERROR("ic12: failed to load required LDBC property columns");
  }

  vid_t root = StorageReadInterface::kInvalidVid;
  if (!graph.GetVertexIndex(person_label, execution::Value::INT64(person_id),
                            root)) {
    return execution::Context{};
  }

  const vid_t tag_class_vid = ldbc::find_vertex_by_string_prop(
      graph, tag_class_label, "name", tag_class_name);
  if (tag_class_vid == StorageReadInterface::kInvalidVid) {
    return execution::Context{};
  }

  std::vector<uint8_t> tag_set;
  collect_tags_in_class(graph, tag_class_label, tag_label, has_type_label,
                        is_subclass_of_label, tag_class_vid, &tag_set);

  const auto comment_has_creator_in = graph.GetGenericIncomingGraphView(
      person_label, comment_label, has_creator_label);
  const auto comment_reply_of_post_out = graph.GetGenericOutgoingGraphView(
      comment_label, post_label, reply_of_label);
  const auto post_has_tag_out =
      graph.GetGenericOutgoingGraphView(post_label, tag_label, has_tag_label);

  std::priority_queue<PersonResult, std::vector<PersonResult>,
                      PersonResultComparer>
      pq;
  int min_count = 0;

  ldbc::foreach_knows_neighbor(
      graph, person_label, knows_label, root, [&](vid_t friend_vid) {
        int count = 0;
        const auto comments = comment_has_creator_in.get_edges(friend_vid);
        for (auto cit = comments.begin(); cit != comments.end(); ++cit) {
          const vid_t comment_vid = *cit;
          const vid_t post_vid = ldbc::get_single_out_neighbor(
              comment_reply_of_post_out, comment_vid);
          if (post_vid == StorageReadInterface::kInvalidVid) {
            continue;
          }
          const auto tags = post_has_tag_out.get_edges(post_vid);
          for (auto tit = tags.begin(); tit != tags.end(); ++tit) {
            if (tag_set[*tit]) {
              ++count;
              break;
            }
          }
        }
        if (count == 0) {
          return;
        }
        if (pq.size() < kTopN) {
          const int64_t friend_id =
              person_id_col->get_view(friend_vid);
          pq.push({count, friend_vid, friend_id});
          if (pq.size() == kTopN) {
            min_count = pq.top().reply_count;
          }
          return;
        } else if (count > min_count) {
          pq.pop();

          const int64_t friend_id =
              person_id_col->get_view(friend_vid);
          pq.push({count, friend_vid, friend_id});
          min_count = pq.top().reply_count;
          return;
        }

        if (count == min_count) {
          const int64_t friend_id =
              person_id_col->get_view(friend_vid);
          if (friend_id < pq.top().person_id) {
            pq.pop();
            pq.push({count, friend_vid, friend_id});
          }
        }
      });

  std::vector<PersonResult> results;
  results.reserve(pq.size());
  while (!pq.empty()) {
    results.push_back(pq.top());
    pq.pop();
  }

  execution::ValueColumnBuilder<int64_t> person_id_builder;
  execution::ValueColumnBuilder<std::string> first_name_builder;
  execution::ValueColumnBuilder<std::string> last_name_builder;
  execution::ListColumnBuilder tag_names_builder(
      DataType(DataTypeId::kVarchar));
  execution::ValueColumnBuilder<int32_t> reply_count_builder;

  for (size_t i = results.size(); i > 0; --i) {
    const auto& row = results[i - 1];
    person_id_builder.push_back_opt(row.person_id);
    first_name_builder.push_back_opt(
        std::string(first_name_col->get_view(row.person_vid)));
    last_name_builder.push_back_opt(
        std::string(last_name_col->get_view(row.person_vid)));

    std::set<vid_t> distinct_tags;
    const auto comments = comment_has_creator_in.get_edges(row.person_vid);
    for (auto cit = comments.begin(); cit != comments.end(); ++cit) {
      const vid_t post_vid =
          ldbc::get_single_out_neighbor(comment_reply_of_post_out, *cit);
      if (post_vid == StorageReadInterface::kInvalidVid) {
        continue;
      }
      const auto tags = post_has_tag_out.get_edges(post_vid);
      for (auto tit = tags.begin(); tit != tags.end(); ++tit) {
        if (*tit < tag_set.size() && tag_set[*tit]) {
          distinct_tags.insert(*tit);
        }
      }
    }
    std::vector<execution::Value> tag_values;
    tag_values.reserve(distinct_tags.size());
    for (vid_t tag_vid : distinct_tags) {
      tag_values.emplace_back(execution::Value::STRING(
          std::string(tag_name_col->get_view(tag_vid))));
    }
    tag_names_builder.push_back_elem(execution::Value::LIST(
        DataType(DataTypeId::kVarchar), std::move(tag_values)));
    reply_count_builder.push_back_opt(row.reply_count);
  }

  return ldbc::make_output_context(
      args.output_aliases,
      {person_id_builder.finish(), first_name_builder.finish(),
       last_name_builder.finish(), tag_names_builder.finish(),
       reply_count_builder.finish()});
}

}  // namespace

function::function_set IC12Function::getFunctionSet() {
  const auto tag_names_list_type =
      common::DataType::List(common::DataType(common::DataTypeId::kVarchar));
  auto fn = std::make_unique<function::NeugCallFunction>(
      IC12Function::name,
      std::vector<common::DataTypeId>{common::DataTypeId::kInt64,
                                      common::DataTypeId::kVarchar},
      function::call_output_columns{
          function::call_output("personId", common::DataTypeId::kInt64),
          function::call_output("personFirstName",
                                common::DataTypeId::kVarchar),
          function::call_output("personLastName", common::DataTypeId::kVarchar),
          function::call_output("tagNames", tag_names_list_type),
          function::call_output("replyCount", common::DataTypeId::kInt32)});
  fn->bindFunc = bind_ic12;
  fn->execFunc = exec_ic12;
  function::function_set set;
  set.push_back(std::move(fn));
  return set;
}

}  // namespace ldbc_ic
}  // namespace extension
}  // namespace neug
