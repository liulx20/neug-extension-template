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

#include "ic14.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <unordered_map>
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

uint64_t pair_key(vid_t a, vid_t b) {
  return a > b ? (static_cast<uint64_t>(a) | (static_cast<uint64_t>(b) << 32))
               : (static_cast<uint64_t>(b) | (static_cast<uint64_t>(a) << 32));
}

class IC14Scorer {
 public:
  IC14Scorer(const StorageReadInterface& graph, label_t person_label,
             label_t post_label, label_t comment_label,
             label_t has_creator_label, label_t reply_of_label)
      : graph_(graph),
        person_label_(person_label),
        post_label_(post_label),
        comment_label_(comment_label),
        post_creator_out_(graph.GetGenericOutgoingGraphView(
            post_label, person_label, has_creator_label)),
        comment_creator_out_(graph.GetGenericOutgoingGraphView(
            comment_label, person_label, has_creator_label)),
        comment_creator_in_(graph.GetGenericIncomingGraphView(
            person_label, comment_label, has_creator_label)),
        post_creator_in_(graph.GetGenericIncomingGraphView(
            person_label, post_label, has_creator_label)),
        comment_reply_post_out_(graph.GetGenericOutgoingGraphView(
            comment_label, post_label, reply_of_label)),
        comment_reply_comment_out_(graph.GetGenericOutgoingGraphView(
            comment_label, comment_label, reply_of_label)),
        comment_reply_post_in_(graph.GetGenericIncomingGraphView(
            post_label, comment_label, reply_of_label)),
        comment_reply_comment_in_(graph.GetGenericIncomingGraphView(
            comment_label, comment_label, reply_of_label)) {}

  int pair_score(vid_t x, vid_t y) const {
    int score = 0;
    const auto x_comments = comment_creator_in_.get_edges(x);
    for (auto it = x_comments.begin(); it != x_comments.end(); ++it) {
      const vid_t comment_vid = *it;
      const vid_t post_vid =
          ldbc::get_single_out_neighbor(comment_reply_post_out_, comment_vid);
      if (post_vid != StorageReadInterface::kInvalidVid) {
        const vid_t author =
            ldbc::get_single_out_neighbor(post_creator_out_, post_vid);
        if (author == y) {
          score += 2;
        }
      } else {
        const vid_t parent = ldbc::get_single_out_neighbor(
            comment_reply_comment_out_, comment_vid);
        if (parent != StorageReadInterface::kInvalidVid) {
          const vid_t author =
              ldbc::get_single_out_neighbor(comment_creator_out_, parent);
          if (author == y) {
            score += 1;
          }
        }
      }
    }
    const auto y_comments = comment_creator_in_.get_edges(y);
    for (auto it = y_comments.begin(); it != y_comments.end(); ++it) {
      const vid_t comment_vid = *it;
      const vid_t post_vid =
          ldbc::get_single_out_neighbor(comment_reply_post_out_, comment_vid);
      if (post_vid != StorageReadInterface::kInvalidVid) {
        const vid_t author =
            ldbc::get_single_out_neighbor(post_creator_out_, post_vid);
        if (author == x) {
          score += 2;
        }
      } else {
        const vid_t parent = ldbc::get_single_out_neighbor(
            comment_reply_comment_out_, comment_vid);
        if (parent != StorageReadInterface::kInvalidVid) {
          const vid_t author =
              ldbc::get_single_out_neighbor(comment_creator_out_, parent);
          if (author == x) {
            score += 1;
          }
        }
      }
    }
    return score;
  }

  int pair_score_one(vid_t root, vid_t other) const {
    const size_t root_degree = ldbc::count_edges(comment_creator_in_, root) +
                               ldbc::count_edges(post_creator_in_, root);
    const size_t other_degree = ldbc::count_edges(comment_creator_in_, other) +
                                ldbc::count_edges(post_creator_in_, other);
    const vid_t u = root_degree > other_degree ? other : root;
    const vid_t v = root_degree > other_degree ? root : other;

    int score = 0;
    const auto comments = comment_creator_in_.get_edges(u);
    for (auto it = comments.begin(); it != comments.end(); ++it) {
      const vid_t comment_vid = *it;
      const vid_t post_vid =
          ldbc::get_single_out_neighbor(comment_reply_post_out_, comment_vid);
      if (post_vid != StorageReadInterface::kInvalidVid) {
        const vid_t author =
            ldbc::get_single_out_neighbor(post_creator_out_, post_vid);
        if (author == v) {
          score += 2;
        }
      } else {
        const vid_t parent = ldbc::get_single_out_neighbor(
            comment_reply_comment_out_, comment_vid);
        if (parent != StorageReadInterface::kInvalidVid) {
          const vid_t author =
              ldbc::get_single_out_neighbor(comment_creator_out_, parent);
          if (author == v) {
            score += 1;
          }
        }
      }
      const auto followers = comment_reply_comment_in_.get_edges(comment_vid);
      for (auto fit = followers.begin(); fit != followers.end(); ++fit) {
        const vid_t author =
            ldbc::get_single_out_neighbor(comment_creator_out_, *fit);
        if (author == v) {
          score += 1;
        }
      }
    }
    const auto posts = post_creator_in_.get_edges(u);
    for (auto it = posts.begin(); it != posts.end(); ++it) {
      const auto replies = comment_reply_post_in_.get_edges(*it);
      for (auto rit = replies.begin(); rit != replies.end(); ++rit) {
        const vid_t author =
            ldbc::get_single_out_neighbor(comment_creator_out_, *rit);
        if (author == v) {
          score += 2;
        }
      }
    }
    return score;
  }

  void accumulate_scores(vid_t root, std::vector<int>* counts) const {
    const auto comments = comment_creator_in_.get_edges(root);
    for (auto it = comments.begin(); it != comments.end(); ++it) {
      const vid_t comment_vid = *it;
      const vid_t post_vid =
          ldbc::get_single_out_neighbor(comment_reply_post_out_, comment_vid);
      if (post_vid != StorageReadInterface::kInvalidVid) {
        const vid_t author =
            ldbc::get_single_out_neighbor(post_creator_out_, post_vid);
        if (author < counts->size() && (*counts)[author] != 0) {
          (*counts)[author] += 2;
        }
      } else {
        const vid_t parent = ldbc::get_single_out_neighbor(
            comment_reply_comment_out_, comment_vid);
        if (parent != StorageReadInterface::kInvalidVid) {
          const vid_t author =
              ldbc::get_single_out_neighbor(comment_creator_out_, parent);
          if (author < counts->size() && (*counts)[author] != 0) {
            (*counts)[author] += 1;
          }
        }
      }
      const auto followers = comment_reply_comment_in_.get_edges(comment_vid);
      for (auto fit = followers.begin(); fit != followers.end(); ++fit) {
        const vid_t author =
            ldbc::get_single_out_neighbor(comment_creator_out_, *fit);
        if (author < counts->size() && (*counts)[author] != 0) {
          (*counts)[author] += 1;
        }
      }
    }
    const auto posts = post_creator_in_.get_edges(root);
    for (auto it = posts.begin(); it != posts.end(); ++it) {
      const auto replies = comment_reply_post_in_.get_edges(*it);
      for (auto rit = replies.begin(); rit != replies.end(); ++rit) {
        const vid_t author =
            ldbc::get_single_out_neighbor(comment_creator_out_, *rit);
        if (author < counts->size() && (*counts)[author] != 0) {
          (*counts)[author] += 2;
        }
      }
    }
  }

 private:
  const StorageReadInterface& graph_;
  label_t person_label_;
  label_t post_label_;
  label_t comment_label_;
  CsrView post_creator_out_;
  CsrView comment_creator_out_;
  CsrView comment_creator_in_;
  CsrView post_creator_in_;
  CsrView comment_reply_post_out_;
  CsrView comment_reply_comment_out_;
  CsrView comment_reply_post_in_;
  CsrView comment_reply_comment_in_;
};

void bfs_layer(const CsrView& out_view, const CsrView& in_view, int8_t depth,
               std::queue<vid_t>* curr, std::queue<vid_t>* next,
               std::vector<int8_t>* dist0, const std::vector<int8_t>& dist1,
               std::vector<vid_t>* meet_points) {
  while (!curr->empty()) {
    const vid_t x = curr->front();
    curr->pop();
    const auto out_edges = out_view.get_edges(x);
    for (auto it = out_edges.begin(); it != out_edges.end(); ++it) {
      const vid_t v = *it;
      if ((*dist0)[v] == -1) {
        (*dist0)[v] = depth;
        next->push(v);
        if (dist1[v] != -1) {
          meet_points->push_back(v);
        }
      }
    }
    const auto in_edges = in_view.get_edges(x);
    for (auto it = in_edges.begin(); it != in_edges.end(); ++it) {
      const vid_t v = *it;
      if ((*dist0)[v] == -1) {
        (*dist0)[v] = depth;
        next->push(v);
        if (dist1[v] != -1) {
          meet_points->push_back(v);
        }
      }
    }
  }
}

void dfs_paths(const CsrView& out_view, const CsrView& in_view, vid_t src,
               vid_t dst, const std::vector<int8_t>& dist_from_src,
               const std::vector<bool>& on_path, std::vector<vid_t>* path,
               std::vector<std::vector<vid_t>>* paths) {
  path->push_back(src);
  if (src == dst) {
    paths->push_back(*path);
    path->pop_back();
    return;
  }
  const auto out_edges = out_view.get_edges(src);
  for (auto it = out_edges.begin(); it != out_edges.end(); ++it) {
    const vid_t v = *it;
    if (on_path[v] && dist_from_src[v] == dist_from_src[src] + 1) {
      dfs_paths(out_view, in_view, v, dst, dist_from_src, on_path, path, paths);
    }
  }
  const auto in_edges = in_view.get_edges(src);
  for (auto it = in_edges.begin(); it != in_edges.end(); ++it) {
    const vid_t v = *it;
    if (on_path[v] && dist_from_src[v] == dist_from_src[src] + 1) {
      dfs_paths(out_view, in_view, v, dst, dist_from_src, on_path, path, paths);
    }
  }
  path->pop_back();
}

std::vector<int> score_paths(const StorageReadInterface& graph,
                             label_t person_label, label_t post_label,
                             label_t comment_label, label_t has_creator_label,
                             label_t reply_of_label,
                             const std::vector<std::vector<vid_t>>& paths) {
  IC14Scorer scorer(graph, person_label, post_label, comment_label,
                    has_creator_label, reply_of_label);
  std::vector<int> scores;
  scores.reserve(paths.size());

  if (paths.size() == 1) {
    int score = 0;
    const auto& path = paths[0];
    for (size_t i = 1; i < path.size(); ++i) {
      score += scorer.pair_score(path[i - 1], path[i]);
    }
    scores.push_back(score);
    return scores;
  }

  std::map<vid_t, std::set<vid_t>> person_pairs;
  for (const auto& path : paths) {
    for (size_t i = 1; i < path.size(); ++i) {
      person_pairs[path[i - 1]].insert(path[i]);
      person_pairs[path[i]].insert(path[i - 1]);
    }
  }

  const size_t person_num = graph.GetVertexSet(person_label).size();
  std::vector<int> counts(person_num, 0);
  std::unordered_map<uint64_t, int> score_cache;

  while (!person_pairs.empty()) {
    vid_t root = std::numeric_limits<vid_t>::max();
    size_t max_degree = 0;
    for (const auto& [vid, neighbors] : person_pairs) {
      if (neighbors.size() > max_degree) {
        max_degree = neighbors.size();
        root = vid;
      }
    }
    if (root == std::numeric_limits<vid_t>::max()) {
      break;
    }
    auto& nbr_set = person_pairs[root];
    if (nbr_set.empty()) {
      break;
    }
    if (nbr_set.size() == 1) {
      const vid_t other = *nbr_set.begin();
      score_cache[pair_key(root, other)] += scorer.pair_score_one(root, other);
      auto& other_set = person_pairs[other];
      other_set.erase(root);
      if (other_set.empty()) {
        person_pairs.erase(other);
      }
    } else {
      for (vid_t v : nbr_set) {
        counts[v] = 1;
      }
      scorer.accumulate_scores(root, &counts);
      for (vid_t v : nbr_set) {
        score_cache[pair_key(root, v)] += (counts[v] - 1);
        counts[v] = 0;
        auto& other_set = person_pairs[v];
        other_set.erase(root);
        if (other_set.empty()) {
          person_pairs.erase(v);
        }
      }
    }
    person_pairs.erase(root);
  }

  for (const auto& path : paths) {
    int score = 0;
    for (size_t i = 1; i < path.size(); ++i) {
      score += score_cache.at(pair_key(path[i - 1], path[i]));
    }
    scores.push_back(score);
  }
  return scores;
}

std::unique_ptr<function::CallFuncInputBase> bind_ic14(
    const Schema& /*schema*/, const execution::ContextMeta& /*ctx_meta*/,
    const ::physical::PhysicalPlan& plan, int op_idx) {
  const auto& params =
      plan.plan(op_idx).opr().procedure_call().query().arguments();
  if (params.size() < 2) {
    THROW_INVALID_ARGUMENT_EXCEPTION(
        "ic14 requires 2 arguments: person1Id and person2Id");
  }
  auto input = std::make_unique<IC14FuncInput>();
  ldbc::bind_ldbc_call(plan, op_idx, input.get());
  return input;
}

execution::Context exec_ic14(const function::CallFuncInputBase& input,
                             IStorageInterface& graph_iface,
                             const execution::ParamsMap& params) {
  const auto& args = dynamic_cast<const IC14FuncInput&>(input);
  const int64_t person1_id = params.at("person1Id").GetValue<int64_t>();
  const int64_t person2_id = params.at("person2Id").GetValue<int64_t>();
  const auto& graph = dynamic_cast<const StorageReadInterface&>(graph_iface);
  const auto& schema = graph.schema();

  const label_t person_label = schema.get_vertex_label_id("PERSON");
  const label_t post_label = schema.get_vertex_label_id("POST");
  const label_t comment_label = schema.get_vertex_label_id("COMMENT");
  const label_t knows_label = schema.get_edge_label_id("KNOWS");
  const label_t has_creator_label = schema.get_edge_label_id("HASCREATOR");
  const label_t reply_of_label = schema.get_edge_label_id("REPLYOF");

  auto person_id_col = ldbc::get_vertex_column<int64_t>(graph, person_label, "id");

  vid_t src = StorageReadInterface::kInvalidVid;
  vid_t dst = StorageReadInterface::kInvalidVid;
  if (!graph.GetVertexIndex(person_label, execution::Value::INT64(person1_id),
                            src) ||
      !graph.GetVertexIndex(person_label, execution::Value::INT64(person2_id),
                            dst)) {
    return execution::Context{};
  }

  const size_t person_num = graph.GetVertexSet(person_label).size();
  std::vector<int8_t> dist_from_src(person_num, -1);
  std::vector<int8_t> dist_from_dst(person_num, -1);
  dist_from_src[src] = 0;
  dist_from_dst[dst] = 0;

  const auto knows_out = graph.GetGenericOutgoingGraphView(
      person_label, person_label, knows_label);
  const auto knows_in = graph.GetGenericIncomingGraphView(
      person_label, person_label, knows_label);

  std::queue<vid_t> q1;
  std::queue<vid_t> q2;
  std::queue<vid_t> tmp;
  q1.push(src);
  q2.push(dst);
  std::vector<vid_t> meet_points;
  int8_t src_depth = 0;
  int8_t dst_depth = 0;

  while (true) {
    if (!q1.empty() && q1.size() <= q2.size()) {
      ++src_depth;
      bfs_layer(knows_out, knows_in, src_depth, &q1, &tmp, &dist_from_src,
                dist_from_dst, &meet_points);
      if (!meet_points.empty()) {
        break;
      }
      std::swap(q1, tmp);
    } else if (!q2.empty()) {
      ++dst_depth;
      bfs_layer(knows_out, knows_in, dst_depth, &q2, &tmp, &dist_from_dst,
                dist_from_src, &meet_points);
      if (!meet_points.empty()) {
        break;
      }
      std::swap(q2, tmp);
    } else {
      break;
    }
    if (q1.empty() || q2.empty()) {
      break;
    }
  }

  if (meet_points.empty()) {
    return execution::Context{};
  }

  std::vector<bool> on_path(person_num, false);
  std::queue<vid_t> frontier;
  for (vid_t v : meet_points) {
    frontier.push(v);
    on_path[v] = true;
  }

  while (!frontier.empty()) {
    const vid_t v = frontier.front();
    frontier.pop();
    const auto out_edges = knows_out.get_edges(v);
    for (auto it = out_edges.begin(); it != out_edges.end(); ++it) {
      const vid_t nbr = *it;
      if (on_path[nbr]) {
        continue;
      }
      if (dist_from_src[nbr] != -1 &&
          dist_from_src[nbr] + 1 == dist_from_src[v]) {
        on_path[nbr] = true;
        frontier.push(nbr);
      }
      if (dist_from_dst[nbr] != -1 &&
          dist_from_dst[nbr] + 1 == dist_from_dst[v]) {
        on_path[nbr] = true;
        frontier.push(nbr);
        dist_from_src[nbr] = dist_from_src[v] + 1;
      }
    }
    const auto in_edges = knows_in.get_edges(v);
    for (auto it = in_edges.begin(); it != in_edges.end(); ++it) {
      const vid_t nbr = *it;
      if (on_path[nbr]) {
        continue;
      }
      if (dist_from_src[nbr] != -1 &&
          dist_from_src[nbr] + 1 == dist_from_src[v]) {
        on_path[nbr] = true;
        frontier.push(nbr);
      }
      if (dist_from_dst[nbr] != -1 &&
          dist_from_dst[nbr] + 1 == dist_from_dst[v]) {
        on_path[nbr] = true;
        frontier.push(nbr);
        dist_from_src[nbr] = dist_from_src[v] + 1;
      }
    }
  }

  std::vector<vid_t> path;
  std::vector<std::vector<vid_t>> paths;
  dfs_paths(knows_out, knows_in, src, dst, dist_from_src, on_path, &path,
            &paths);
  if (paths.empty()) {
    return execution::Context{};
  }

  const std::vector<int> scores =
      score_paths(graph, person_label, post_label, comment_label,
                  has_creator_label, reply_of_label, paths);

  std::vector<size_t> order(paths.size());
  for (size_t i = 0; i < paths.size(); ++i) {
    order[i] = i;
  }
  std::sort(order.begin(), order.end(),
            [&](size_t a, size_t b) { return scores[a] > scores[b]; });

  execution::ListColumnBuilder path_ids_builder(DataType(DataTypeId::kInt64));
  execution::ValueColumnBuilder<double> weight_builder;

  for (size_t idx : order) {
    std::vector<execution::Value> ids;
    ids.reserve(paths[idx].size());
    for (vid_t v : paths[idx]) {
      ids.emplace_back(execution::Value::INT64(
          person_id_col->get_view(v)));
    }
    path_ids_builder.push_back_elem(
        execution::Value::LIST(DataType(DataTypeId::kInt64), std::move(ids)));
    weight_builder.push_back_opt(static_cast<double>(scores[idx]) / 2.0);
  }

  return ldbc::make_output_context(
      args.output_aliases,
      {path_ids_builder.finish(), weight_builder.finish()});
}

}  // namespace

function::function_set IC14Function::getFunctionSet() {
  const auto person_ids_in_path_type =
      common::DataType::List(common::DataType(common::DataTypeId::kInt64));
  auto fn = std::make_unique<function::NeugCallFunction>(
      IC14Function::name,
      std::vector<common::DataTypeId>{common::DataTypeId::kInt64,
                                      common::DataTypeId::kInt64},
      function::call_output_columns{
          {"personIdsInPath", person_ids_in_path_type},
          {"pathWeight", common::DataType(common::DataTypeId::kDouble)}});
  fn->bindFunc = bind_ic14;
  fn->execFunc = exec_ic14;
  function::function_set set;
  set.push_back(std::move(fn));
  return set;
}

}  // namespace ldbc_ic
}  // namespace extension
}  // namespace neug
