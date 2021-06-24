#pragma once

#include <handlegraph/path_handle_graph.hpp>
#include <handlegraph/handle_graph.hpp>
#include <handlegraph/util.hpp>
#include <vector>
#include <set>
#include <deque>
#include "hash_map.hpp"
#include "ips4o.hpp"

namespace odgi {
namespace algorithms {
    
using namespace handlegraph;

class segment_map_t {
public:
    // each segment is identified by its starting step
    std::vector<step_handle_t> segment_cut;
    // and a length
    std::vector<uint64_t> segment_length;
    // maps from node id-1 to idx in segments
    std::vector<uint64_t> node_idx;
    // stores segment assignments sorted by node
    // segment ids stored here map into segment_cuts and segment_lengths
    std::vector<uint64_t> segments;
    segment_map_t(const PathHandleGraph& graph,
                  const std::vector<path_handle_t>& paths,
                  const ska::flat_hash_map<step_handle_t, uint64_t>& step_pos,    
                  const uint64_t& merge_dist,
                  const size_t& num_threads);
    void for_segment_on_node(
        uint64_t node_id,
        const std::function<void(const uint64_t& segment_id)>& func) const;
    uint64_t get_segment_length(const uint64_t& segment_id) const;
    std::vector<std::pair<double, uint64_t>> get_matches(
        const PathHandleGraph& graph,
        const step_handle_t& start,
        const step_handle_t& end,
        const uint64_t& query_length) const;
    const step_handle_t& get_segment_cut(
        const uint64_t& idx) const;
};

std::vector<step_handle_t> untangle_cuts(
    const PathHandleGraph& graph,
    const step_handle_t& start,
    const step_handle_t& end,
    const ska::flat_hash_map<step_handle_t, uint64_t>& step_pos);

std::vector<step_handle_t> merge_cuts(
    const std::vector<step_handle_t>& cuts,
    const uint64_t& dist,
    const ska::flat_hash_map<step_handle_t, uint64_t>& step_pos);

void write_cuts(
    const PathHandleGraph& graph,
    const path_handle_t& path,
    const std::vector<step_handle_t>& cuts,
    const ska::flat_hash_map<step_handle_t, uint64_t>& path_pos);

void self_dotplot(
    const PathHandleGraph& graph,
    const path_handle_t& path);

ska::flat_hash_map<step_handle_t, uint64_t> make_step_index(
    const PathHandleGraph& graph,
    const std::vector<path_handle_t>& paths);

void map_segments(
    const PathHandleGraph& graph,
    const path_handle_t& path,
    const std::vector<step_handle_t>& cuts,
    const segment_map_t& target_segments,
    const ska::flat_hash_map<step_handle_t, uint64_t>& step_pos);

void untangle(
    const PathHandleGraph& graph,
    const std::vector<path_handle_t>& queries,
    const std::vector<path_handle_t>& targets,
    const uint64_t& merge_dist,
    const size_t& num_threads);

}
}


