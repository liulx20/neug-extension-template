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

#include "ic5.h"

#include <array>
#include <map>
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
class IC5 {
public:
static constexpr size_t kTopN = 20;

struct ForumInfo {
  vid_t forum_vid = 0;
  int post_count = 0;
  int64_t forum_id = 0;
};

struct ForumInfoComparer {
  bool operator()(const ForumInfo& lhs, const ForumInfo& rhs) const {
    if (lhs.post_count > rhs.post_count) {
      return true;
    }
    if (lhs.post_count < rhs.post_count) {
      return false;
    }
    return lhs.forum_id < rhs.forum_id;
  }
};

static void collect_knows_1d_2d_neighbors(const StorageReadInterface& graph,
                                   label_t person_label, label_t knows_label,
                                   vid_t root, std::vector<vid_t>* friends) {
  const auto out_view = graph.GetGenericOutgoingGraphView(
      person_label, person_label, knows_label);
  const auto in_view = graph.GetGenericIncomingGraphView(
      person_label, person_label, knows_label);

  const size_t person_num = graph.GetVertexSet(person_label).size();
  std::vector<bool> seen(person_num, false);
  seen[root] = true;

  std::vector<vid_t> neighbors;
  const auto root_in = in_view.get_edges(root);
  for (auto it = root_in.begin(); it != root_in.end(); ++it) {
    neighbors.push_back(*it);
  }
  const auto root_out = out_view.get_edges(root);
  for (auto it = root_out.begin(); it != root_out.end(); ++it) {
    neighbors.push_back(*it);
  }

  for (vid_t v : neighbors) {
    if (!seen[v]) {
      seen[v] = true;
      friends->push_back(v);
    }
    const auto v_in = in_view.get_edges(v);
    for (auto it = v_in.begin(); it != v_in.end(); ++it) {
      if (!seen[*it]) {
        seen[*it] = true;
        friends->push_back(*it);
      }
    }
    const auto v_out = out_view.get_edges(v);
    for (auto it = v_out.begin(); it != v_out.end(); ++it) {
      if (!seen[*it]) {
        seen[*it] = true;
        friends->push_back(*it);
      }
    }
  }
}

static std::unique_ptr<function::CallFuncInputBase> bind(
    const Schema& /*schema*/, const execution::ContextMeta& /*ctx_meta*/,
    const ::physical::PhysicalPlan& plan, int op_idx) {
  const auto& params =
      plan.plan(op_idx).opr().procedure_call().query().arguments();
  if (params.size() < 2) {
    THROW_INVALID_ARGUMENT_EXCEPTION(
        "ic5 requires 2 arguments: personId and minDate");
  }
  auto input = std::make_unique<IC5FuncInput>();
  ldbc::bind_ldbc_call(plan, op_idx, input.get());
  return input;
}

static execution::Context exec(const function::CallFuncInputBase& input,
                            IStorageInterface& graph_iface) {
  const auto& ic5_input = dynamic_cast<const IC5FuncInput&>(input);
  const int64_t person_id = ic5_input.person_id;
  const int64_t min_date_ms = ic5_input.min_date_ms;
  const auto& graph = dynamic_cast<const StorageReadInterface&>(graph_iface);
  const auto& schema = graph.schema();

  const label_t forum_label = schema.get_vertex_label_id("FORUM");
  const label_t person_label = schema.get_vertex_label_id("PERSON");
  const label_t post_label = schema.get_vertex_label_id("POST");
  const label_t knows_label = schema.get_edge_label_id("KNOWS");
  const label_t has_creator_label = schema.get_edge_label_id("HASCREATOR");
  const label_t has_member_label = schema.get_edge_label_id("HASMEMBER");
  const label_t container_of_label = schema.get_edge_label_id("CONTAINEROF");

  auto forum_title_col =
      ldbc::get_vertex_column<std::string_view>(graph, forum_label, "title");
  auto forum_id_col = ldbc::get_vertex_column<int64_t>(graph, forum_label, "id");
  if (!forum_title_col) {
    THROW_RUNTIME_ERROR("ic5: failed to load required LDBC property columns");
  }

  vid_t root = StorageReadInterface::kInvalidVid;
  if (!graph.GetVertexIndex(person_label, Value::INT64(person_id),
                            root)) {
    return execution::Context{};
  }

  const size_t forum_num = graph.GetVertexSet(forum_label).size();
  std::map<vid_t, int> post_count;
  std::vector<bool> accessed_forum_set(forum_num, false);
  std::vector<bool> person_forum_set(forum_num, false);
  std::vector<vid_t> forum_list;
  std::vector<vid_t> person_forum_list;
  forum_list.reserve(256);
  person_forum_list.reserve(32);

  const auto post_has_creator_in = graph.GetGenericIncomingGraphView(
      person_label, post_label, has_creator_label);
  const auto forum_container_of_post_in = graph.GetGenericIncomingGraphView(
      post_label, forum_label, container_of_label);
  const DateTime min_date(min_date_ms);

  const auto has_member_in = ldbc::get_typed_incoming_view(
      graph, person_label, forum_label, has_member_label);

  std::vector<vid_t> friends_list;
  collect_knows_1d_2d_neighbors(graph, person_label, knows_label, root,
                                &friends_list);

  for (vid_t friend_vid : friends_list) {
    ldbc::foreach_incoming_nbr_gt(
        has_member_in, friend_vid, min_date,
        [&](vid_t forum_vid, const DateTime& /*join_date*/) {
          person_forum_set[forum_vid] = true;
          person_forum_list.push_back(forum_vid);
          if (!accessed_forum_set[forum_vid]) {
            accessed_forum_set[forum_vid] = true;
            forum_list.push_back(forum_vid);
          }
        });
    if (person_forum_list.empty()) {
      continue;
    }

    const auto posts = post_has_creator_in.get_edges(friend_vid);
    for (auto it = posts.begin(); it != posts.end(); ++it) {
      const vid_t post_vid = *it;
      const vid_t forum_vid =
          ldbc::get_single_out_neighbor(forum_container_of_post_in, post_vid);
      if (forum_vid >= forum_num || !person_forum_set[forum_vid]) {
        continue;
      }
      ++post_count[forum_vid];
    }

    for (vid_t forum_vid : person_forum_list) {
      person_forum_set[forum_vid] = false;
    }
    person_forum_list.clear();
  }

  ForumInfoComparer cmp;
  std::priority_queue<ForumInfo, std::vector<ForumInfo>, ForumInfoComparer> que(
      cmp);
  for (const auto& pair : post_count) {
    const vid_t forum_vid = pair.first;
    const int count = pair.second;
    accessed_forum_set[forum_vid] = false;
    if (que.size() < kTopN) {
      que.emplace(
          forum_vid, count,
          forum_id_col->get_view(forum_vid));
      continue;
    }
    const auto& top = que.top();
    if (top.post_count < count) {
      que.pop();
      que.emplace(
          forum_vid, count,
          forum_id_col->get_view(forum_vid));
    } else if (top.post_count == count) {
      const int64_t forum_id =
          forum_id_col->get_view(forum_vid);
      if (forum_id < top.forum_id) {
        que.pop();
        que.emplace(forum_vid, count, forum_id);
      }
    }
  }

  if (que.size() < kTopN) {
    for (vid_t forum_vid : forum_list) {
      if (!accessed_forum_set[forum_vid]) {
        continue;
      }
      accessed_forum_set[forum_vid] = false;
      if (que.size() < kTopN) {
        que.emplace(
            forum_vid, 0,
            forum_id_col->get_view(forum_vid));
        continue;
      }
      const auto& top = que.top();
      const int64_t forum_id =
          forum_id_col->get_view(forum_vid);
      if (forum_id < top.forum_id) {
        que.pop();
        que.emplace(forum_vid, 0, forum_id);
      }
    }
  } else if (forum_list.size() * 8 < forum_num) {
    for (vid_t forum_vid : forum_list) {
      if (forum_vid < forum_num) {
        accessed_forum_set[forum_vid] = false;
      }
    }
  } else {
    accessed_forum_set.clear();
  }

  forum_list.clear();

  std::vector<ForumInfo> results;
  results.reserve(que.size());
  while (!que.empty()) {
    results.push_back(que.top());
    que.pop();
  }

  ValueColumnBuilder<std::string> forum_title_builder;
  ValueColumnBuilder<int32_t> post_count_builder;
  forum_title_builder.reserve(results.size());
  post_count_builder.reserve(results.size());
  for (size_t i = results.size(); i > 0; --i) {
    const auto& row = results[i - 1];
    forum_title_builder.push_back_opt(
        std::string(forum_title_col->get_view(row.forum_vid)));
    post_count_builder.push_back_opt(row.post_count);
  }

  execution::ContextChunk chunk;
  chunk.set(0, forum_title_builder.finish());
  chunk.set(1, post_count_builder.finish());
  execution::Context ctx;
  ctx.append_chunk(std::move(chunk));
  ctx.tag_ids = ic5_input.output_aliases;
  return ctx;
}

};

function::function_set IC5Function::getFunctionSet() {
  auto function = std::make_unique<function::NeugCallFunction>(
      IC5Function::name,
      function::call_input_types{common::DataType(common::DataTypeId::kInt64),
                                      common::DataType(common::DataTypeId::kInt64)},
      function::call_output_columns{
          {"forumTitle", common::DataType(common::DataTypeId::kVarchar)},
          {"postCount", common::DataType(common::DataTypeId::kInt32)}});
  function->bindFunc = IC5::bind;
  function->execFunc = IC5::exec;
  function::function_set function_set;
  function_set.push_back(std::move(function));
  return function_set;
}

}  // namespace ldbc_ic
}  // namespace extension
}  // namespace neug
