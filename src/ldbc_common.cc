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

#include "ldbc_common.h"

#include <string>

#include "neug/execution/common/context_chunk.h"
#include "neug/utils/exception/exception.h"

namespace neug {
namespace extension {
namespace ldbc {

int64_t parse_i64_arg(const ::common::Value& value, const char* arg_name) {
  if (value.has_i64()) {
    return value.i64();
  }
  if (value.has_i32()) {
    return value.i32();
  }
  THROW_INVALID_ARGUMENT_EXCEPTION(
      std::string("ldbc: argument ") + arg_name + " must be an integer");
}

std::string parse_string_arg(const ::common::Value& value,
                             const char* arg_name) {
  if (value.has_str()) {
    return value.str();
  }
  THROW_INVALID_ARGUMENT_EXCEPTION(
      std::string("ldbc: argument ") + arg_name + " must be a string");
}

void bind_output_aliases(const ::physical::PhysicalPlan& plan, int op_idx,
                         std::vector<int>* output_aliases) {
  const auto& physical_op = plan.plan(op_idx);
  output_aliases->reserve(physical_op.meta_data_size());
  for (int i = 0; i < physical_op.meta_data_size(); ++i) {
    output_aliases->push_back(physical_op.meta_data(i).alias());
  }
}

execution::Context make_output_context(
    const std::vector<int>& output_aliases,
    const std::vector<std::shared_ptr<execution::IContextColumn>>& columns) {
  execution::Context ctx;
  execution::ContextChunk out_chunk;
  ctx.tag_ids = output_aliases;
  for (size_t i = 0; i < output_aliases.size(); ++i) {
    out_chunk.set(output_aliases[i], columns[i]);
  }
  ctx.append_chunk(std::move(out_chunk));
  return ctx;
}

bool find_message_vertex(const StorageReadInterface& graph, label_t post_label,
                         label_t comment_label, int64_t message_id,
                         vid_t* message_vid, bool* is_post) {
  vid_t vid = StorageReadInterface::kInvalidVid;
  if (graph.GetVertexIndex(post_label, execution::Value::INT64(message_id),
                         vid)) {
    *message_vid = vid;
    *is_post = true;
    return true;
  }
  if (graph.GetVertexIndex(comment_label, execution::Value::INT64(message_id),
                         vid)) {
    *message_vid = vid;
    *is_post = false;
    return true;
  }
  return false;
}

vid_t resolve_root_post(const StorageReadInterface& graph, label_t post_label,
                        label_t comment_label, label_t reply_of_label,
                        vid_t message_vid, bool is_post) {
  if (is_post) {
    return message_vid;
  }
  const auto comment_to_post = graph.GetGenericOutgoingGraphView(
      comment_label, post_label, reply_of_label);
  const auto comment_to_comment = graph.GetGenericOutgoingGraphView(
      comment_label, comment_label, reply_of_label);
  vid_t current = message_vid;
  while (true) {
    const vid_t post_vid = get_single_out_neighbor(comment_to_post, current);
    if (post_vid != StorageReadInterface::kInvalidVid) {
      return post_vid;
    }
    const vid_t parent = get_single_out_neighbor(comment_to_comment, current);
    if (parent == StorageReadInterface::kInvalidVid) {
      return StorageReadInterface::kInvalidVid;
    }
    current = parent;
  }
}

std::string message_content(const StorageReadInterface& graph,
                            label_t post_label, label_t comment_label,
                            vid_t message_vid, bool is_post) {
  if (is_post) {
    auto content_col =
        get_vertex_column<std::string_view>(graph, post_label, "content");
    auto image_col =
        get_vertex_column<std::string_view>(graph, post_label, "imageFile");
    auto length_col =
        get_vertex_column<int32_t>(graph, post_label, "length");
    if (!content_col || !image_col || !length_col) {
      return "";
    }
    const auto& content = length_col->get_view(message_vid) == 0
                              ? image_col->get_view(message_vid)
                              : content_col->get_view(message_vid);
    return std::string(content);
  }
  auto content_col =
      get_vertex_column<std::string_view>(graph, comment_label, "content");
  return content_col ? std::string(content_col->get_view(message_vid)) : "";
}

vid_t find_vertex_by_string_prop(const StorageReadInterface& graph,
                                 label_t label, const std::string& prop_name,
                                 const std::string& value) {
  auto col = get_vertex_column<std::string_view>(graph, label, prop_name);
  if (!col) {
    return StorageReadInterface::kInvalidVid;
  }
  for (auto it = graph.GetVertexSet(label).begin();
       it != graph.GetVertexSet(label).end(); ++it) {
    if (col->get_view(*it) == value) {
      return *it;
    }
  }
  return StorageReadInterface::kInvalidVid;
}

}  // namespace ldbc
}  // namespace extension
}  // namespace neug
