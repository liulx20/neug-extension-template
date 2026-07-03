/** Copyright 2020 Alibaba Group Holding Limited.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * 	http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "neug/compiler/function/neug_call_function.h"
#include "neug/execution/common/context.h"
#include "neug/execution/common/params_map.h"
#include "neug/storages/csr/csr_view.h"
#include "neug/storages/graph/graph_interface.h"
#include "neug/utils/property/types.h"

namespace neug {
namespace extension {
namespace ldbc {

struct LdbcCallInput : function::CallFuncInputBase {
  std::vector<int> output_aliases;
};

void bind_ldbc_call(const ::physical::PhysicalPlan& plan, int op_idx,
                    LdbcCallInput* input);

template <typename T>
inline std::shared_ptr<StorageReadInterface::vertex_column_t<T>> get_vertex_column(
    const StorageReadInterface& graph, label_t label,
    const std::string& prop_name) {
  return std::dynamic_pointer_cast<StorageReadInterface::vertex_column_t<T>>(
      graph.GetVertexPropColumn(label, prop_name));
}

inline vid_t get_single_out_neighbor(const CsrView& view, vid_t vertex) {
  const auto edges = view.get_edges(vertex);
  for (auto it = edges.begin(); it != edges.end(); ++it) {
    return *it;
  }
  return StorageReadInterface::kInvalidVid;
}

inline size_t count_edges(const CsrView& view, vid_t vertex) {
  size_t count = 0;
  const auto edges = view.get_edges(vertex);
  for (auto it = edges.begin(); it != edges.end(); ++it) {
    ++count;
  }
  return count;
}

void bind_output_aliases(const ::physical::PhysicalPlan& plan, int op_idx,
                         std::vector<int>* output_aliases);

execution::Context make_output_context(
    const std::vector<int>& output_aliases,
    const std::vector<std::shared_ptr<execution::IContextColumn>>& columns);

bool find_message_vertex(const StorageReadInterface& graph, label_t post_label,
                         label_t comment_label, int64_t message_id,
                         vid_t* message_vid, bool* is_post);

vid_t resolve_root_post(const StorageReadInterface& graph, label_t post_label,
                        label_t comment_label, label_t reply_of_label,
                        vid_t message_vid, bool is_post);

std::string message_content(const StorageReadInterface& graph, label_t post_label,
                            label_t comment_label, vid_t message_vid,
                            bool is_post);

template <typename Func>
inline void foreach_knows_neighbor(const StorageReadInterface& graph,
                                   label_t person_label, label_t knows_label,
                                   vid_t root, Func&& func) {
  const auto out_view = graph.GetGenericOutgoingGraphView(
      person_label, person_label, knows_label);
  const auto in_view = graph.GetGenericIncomingGraphView(
      person_label, person_label, knows_label);
  const auto out_edges = out_view.get_edges(root);
  for (auto it = out_edges.begin(); it != out_edges.end(); ++it) {
    std::forward<Func>(func)(*it);
  }
  const auto in_edges = in_view.get_edges(root);
  for (auto it = in_edges.begin(); it != in_edges.end(); ++it) {
    std::forward<Func>(func)(*it);
  }
}

template <typename Func>
inline void foreach_knows_2d_neighbor(const StorageReadInterface& graph,
                                      label_t person_label, label_t knows_label,
                                      vid_t root, Func&& func) {
  const auto out_view = graph.GetGenericOutgoingGraphView(
      person_label, person_label, knows_label);
  const auto in_view = graph.GetGenericIncomingGraphView(
      person_label, person_label, knows_label);

  std::vector<bool> seen(graph.GetVertexSet(person_label).size(), false);
  seen[root] = true;
  std::vector<vid_t> hop1;
  const auto root_in = in_view.get_edges(root);
  for (auto it = root_in.begin(); it != root_in.end(); ++it) {
    seen[*it] = true;
    hop1.push_back(*it);
  }
  const auto root_out = out_view.get_edges(root);
  for (auto it = root_out.begin(); it != root_out.end(); ++it) {
    if (!seen[*it]) {
      seen[*it] = true;
      hop1.push_back(*it);
    }
  }

  for (vid_t v : hop1) {
    const auto v_in = in_view.get_edges(v);
    for (auto it = v_in.begin(); it != v_in.end(); ++it) {
      if (!seen[*it]) {
        seen[*it] = true;
        std::forward<Func>(func)(*it);
      }
    }
    const auto v_out = out_view.get_edges(v);
    for (auto it = v_out.begin(); it != v_out.end(); ++it) {
      if (!seen[*it]) {
        seen[*it] = true;
        std::forward<Func>(func)(*it);
      }
    }
  }
}

template <typename Func>
inline void foreach_knows_1d_2d_neighbor(const StorageReadInterface& graph,
                                           label_t person_label,
                                           label_t knows_label, vid_t root,
                                           Func&& func) {
  const auto out_view = graph.GetGenericOutgoingGraphView(
      person_label, person_label, knows_label);
  const auto in_view = graph.GetGenericIncomingGraphView(
      person_label, person_label, knows_label);

  std::vector<bool> visited(graph.GetVertexSet(person_label).size(), false);
  visited[root] = true;
  std::vector<vid_t> neighbors;
  const auto root_in = in_view.get_edges(root);
  for (auto it = root_in.begin(); it != root_in.end(); ++it) {
    neighbors.push_back(*it);
  }
  const auto root_out = out_view.get_edges(root);
  for (auto it = root_out.begin(); it != root_out.end(); ++it) {
    neighbors.push_back(*it);
  }

  for (vid_t neighbor : neighbors) {
    if (!visited[neighbor]) {
      visited[neighbor] = true;
      std::forward<Func>(func)(neighbor);
    }
    const auto neighbor_in = in_view.get_edges(neighbor);
    for (auto it = neighbor_in.begin(); it != neighbor_in.end(); ++it) {
      if (!visited[*it]) {
        visited[*it] = true;
        std::forward<Func>(func)(*it);
      }
    }
    const auto neighbor_out = out_view.get_edges(neighbor);
    for (auto it = neighbor_out.begin(); it != neighbor_out.end(); ++it) {
      if (!visited[*it]) {
        visited[*it] = true;
        std::forward<Func>(func)(*it);
      }
    }
  }
}

inline void mark_knows_1d_2d_neighbors(const StorageReadInterface& graph,
                                       label_t person_label, label_t knows_label,
                                       vid_t root, std::vector<bool>* friends) {
  friends->assign(graph.GetVertexSet(person_label).size(), false);
  (*friends)[root] = true;
  const auto out_view = graph.GetGenericOutgoingGraphView(
      person_label, person_label, knows_label);
  const auto in_view = graph.GetGenericIncomingGraphView(
      person_label, person_label, knows_label);

  std::vector<vid_t> neighbors;
  const auto root_in = in_view.get_edges(root);
  for (auto it = root_in.begin(); it != root_in.end(); ++it) {
    (*friends)[*it] = true;
    neighbors.push_back(*it);
  }
  const auto root_out = out_view.get_edges(root);
  for (auto it = root_out.begin(); it != root_out.end(); ++it) {
    (*friends)[*it] = true;
    neighbors.push_back(*it);
  }
  for (vid_t neighbor : neighbors) {
    const auto neighbor_in = in_view.get_edges(neighbor);
    for (auto it = neighbor_in.begin(); it != neighbor_in.end(); ++it) {
      (*friends)[*it] = true;
    }
    const auto neighbor_out = out_view.get_edges(neighbor);
    for (auto it = neighbor_out.begin(); it != neighbor_out.end(); ++it) {
      (*friends)[*it] = true;
    }
  }
  (*friends)[root] = false;
}

vid_t find_vertex_by_string_prop(const StorageReadInterface& graph,
                                 label_t label, const std::string& prop_name,
                                 const std::string& value);

inline TypedCsrView<DateTime, CsrViewType::kMultipleMutable>
get_typed_incoming_view(const StorageReadInterface& graph, label_t dst_label,
                        label_t src_label, label_t edge_label) {
  const auto view = graph.GetGenericIncomingGraphView(dst_label, src_label,
                                                      edge_label);
  return view.get_typed_view<DateTime, CsrViewType::kMultipleMutable>();
}

template <typename Func>
inline void foreach_incoming_nbr_lt(const StorageReadInterface& graph,
                                    label_t dst_label, label_t src_label,
                                    label_t edge_label, vid_t dst_vid,
                                    const DateTime& threshold, Func&& func) {
  get_typed_incoming_view(graph, dst_label, src_label, edge_label)
      .foreach_nbr_lt(dst_vid, threshold,
                      [&](vid_t nbr, const DateTime& edge_data) {
                        std::forward<Func>(func)(nbr, edge_data);
                      });
}

template <typename Func>
inline void foreach_incoming_nbr_gt(const StorageReadInterface& graph,
                                    label_t dst_label, label_t src_label,
                                    label_t edge_label, vid_t dst_vid,
                                    const DateTime& threshold, Func&& func) {
  get_typed_incoming_view(graph, dst_label, src_label, edge_label)
      .foreach_nbr_gt(dst_vid, threshold,
                      [&](vid_t nbr, const DateTime& edge_data) {
                        std::forward<Func>(func)(nbr, edge_data);
                      });
}

// Flex foreach_edges_between(v, minDate, maxDate) on incoming DateTime CSR:
// keep edges with minDate <= creationDate <= maxDate (inclusive).
template <typename Func>
inline void foreach_incoming_nbr_between(const StorageReadInterface& graph,
                                         label_t dst_label, label_t src_label,
                                         label_t edge_label, vid_t dst_vid,
                                         int64_t min_date_ms,
                                         int64_t max_date_ms, Func&& func) {
  foreach_incoming_nbr_lt(
      graph, dst_label, src_label, edge_label, dst_vid,
      DateTime(max_date_ms + 1),
      [&](vid_t nbr, const DateTime& edge_data) {
        if (edge_data.milli_second < min_date_ms) {
          return;
        }
        std::forward<Func>(func)(nbr, edge_data);
      });
}

// Flex foreach_edges_between with an exclusive upper bound: [minDate, maxDate).
template <typename Func>
inline void foreach_incoming_nbr_between_half_open(
    const StorageReadInterface& graph, label_t dst_label, label_t src_label,
    label_t edge_label, vid_t dst_vid, int64_t min_date_ms, int64_t max_date_ms,
    Func&& func) {
  foreach_incoming_nbr_lt(
      graph, dst_label, src_label, edge_label, dst_vid, DateTime(max_date_ms),
      [&](vid_t nbr, const DateTime& edge_data) {
        if (edge_data.milli_second < min_date_ms) {
          return;
        }
        std::forward<Func>(func)(nbr, edge_data);
      });
}

}  // namespace ldbc
}  // namespace extension
}  // namespace neug
