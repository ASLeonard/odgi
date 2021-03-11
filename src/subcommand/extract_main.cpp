#include "subcommand.hpp"
#include "odgi.hpp"
#include "args.hxx"
#include <omp.h>
#include <regex>
#include "utils.hpp"

#include "src/algorithms/subgraph/extract.hpp"

namespace odgi {

    using namespace odgi::subcommand;


    int main_extract(int argc, char **argv) {

        // trick argumentparser to do the right thing with the subcommand
        for (uint64_t i = 1; i < argc - 1; ++i) {
            argv[i] = argv[i + 1];
        }
        std::string prog_name = "odgi extract";
        argv[0] = (char *) prog_name.c_str();
        --argc;

        args::ArgumentParser parser("extract parts of the graph as defined by query criteria");
        args::HelpFlag help(parser, "help", "display this help summary", {'h', "help"});

        args::ValueFlag<std::string> og_in_file(parser, "FILE", "load the graph from this file", {'i', "idx"});
        args::ValueFlag<std::string> og_out_file(parser, "FILE", "store the graph self index in this file",
                                                 {'o', "out"});
        args::Flag _split_subgraphs(parser, "split_subgraphs",
                                    "instead of writing the target subgraphs into a single graph, "
                                    "write one subgraph per given target to a separate file named path:start-end.og",
                                    {'s', "split-subgraphs"});

        args::ValueFlag<std::string> _node_list(parser, "FILE", "a file with one node id per line", {'l', "node-list"});
        args::ValueFlag<uint64_t> _target_node(parser, "ID", "a single node from which to begin our traversal",
                                               {'n', "node"});
        args::ValueFlag<uint64_t> _context_size(parser, "N",
                                                "the number of steps away from our initial subgraph that we should collect",
                                                {'c', "context"});
        args::Flag _use_length(parser, "use_length",
                               "treat the context size as a length in bases (and not as a number of steps)",
                               {'L', "use-length"});

        /// Range selection
        args::ValueFlag<std::string> _path_range(parser, "STRING",
                                                 "find the node(s) in the specified path range TARGET=path[:pos1[-pos2]]",
                                                 {'r', "path-range"});
        args::ValueFlag<std::string> _path_bed_file(parser, "FILE",
                                                    "find the node(s) in the path range(s) specified in the given BED FILE",
                                                    {'b', "bed-file"});
        args::Flag _full_range(parser, "use_length",
                               "collects all nodes in the sorted order of the graph in the min and max position touched by the given path ranges. "
                               "Be careful to use it with very complex graphs",
                               {'E', "full-range"});

        args::ValueFlag<uint64_t> nthreads(parser, "N", "number of threads to use (to embed the subpaths in parallel)",
                                           {'t', "threads"});

        args::Flag _debug(parser, "progress", "print information to stderr",
                          {'P', "progress"});

        try {
            parser.ParseCLI(argc, argv);
        } catch (args::Help) {
            std::cout << parser;
            return 0;
        } catch (args::ParseError e) {
            std::cerr << e.what() << std::endl;
            std::cerr << parser;
            return 1;
        }
        if (argc == 1) {
            std::cout << parser;
            return 1;
        }

        if (!og_in_file) {
            std::cerr
                    << "[odgi::extract] error: please specify an input file from where to load the graph via -i=[FILE], --idx=[FILE]."
                    << std::endl;
            return 1;
        }

        if (!_split_subgraphs && !og_out_file) {
            std::cerr << "[odgi::extract] error: please specify an output file to where to store the subgraph via -o=[FILE], --out=[FILE]." << std::endl;
            return 1;
        }

        if (_split_subgraphs && og_out_file) {
            std::cerr << "[odgi::extract] error: please do not specify an output file (with -o/--out) when one subgraph "
                         "per given target is requested (with -s/--split-subgraphs)." << std::endl;
            return 1;
        }

        uint64_t context_size = _context_size ? args::get(_context_size) : 0;

        graph_t graph;
        assert(argc > 0);
        std::string infile = args::get(og_in_file);
        if (!infile.empty()) {
            if (infile == "-") {
                graph.deserialize(std::cin);
            } else {
                ifstream f(infile.c_str());
                graph.deserialize(f);
                f.close();
            }
        }

        if (_full_range) {
            nid_t last_node_id = graph.min_node_id();
            graph.for_each_handle([&](const handle_t &h) {
                nid_t node_id = graph.get_id(h);
                if (node_id - last_node_id > 1) {
                    std::cerr
                            << "[odgi::extract] error: the graph is not optimized. "
                               "To extract the full ranges, please run 'odgi sort' using -O/--optimize first"
                            << std::endl;
                    exit(1);
                }
                last_node_id = node_id;
            });
        }

        std::vector<string> targets_str;
        std::vector<Region> targets;

        // handle targets from BED
        if (_path_bed_file) {
            parse_bed_regions(args::get(_path_bed_file), targets);
        }

        // handle targets from command line
        if (_path_range) {
            targets_str.push_back(args::get(_path_range));
        }

        for (auto &target : targets_str) {
            Region region;
            parse_region(target, region);

            if (!graph.has_path(region.seq)) {
                std::cerr
                        << "[odgi::extract] error: path " << region.seq << " not found in the input graph."
                        << std::endl;
                return 1;
            }

            // no coordinates given, we do whole thing (0,-1)
            if (region.start < 0 && region.end < 0) {
                region.start = 0;
            }

            targets.push_back(region);
        }

        bool debug = args::get(_debug);
        uint64_t num_threads = args::get(nthreads) ? args::get(nthreads) : 1;

        if (!targets.empty()) {
            auto prep_graph = [&](graph_t &source, graph_t &subgraph, uint64_t context_size, bool use_length) {
                if (context_size > 0) {
                    if (debug) {
                        std::cerr << "[odgi::extract] expansion and adding connecting edges" << std::endl;
                    }

                    if (use_length) {
                        algorithms::expand_subgraph_by_length(source, subgraph, context_size, false);
                    } else {
                        algorithms::expand_subgraph_by_steps(source, subgraph, context_size, false);
                    }
                } else {
                    if (debug) {
                        std::cerr << "[odgi::extract] adding connecting edges" << std::endl;
                    }

                    algorithms::add_connecting_edges_to_subgraph(source, subgraph);
                }

                if (debug) {
                    std::cerr << "[odgi::extract] adding subpaths" << std::endl;
                }
                algorithms::add_subpaths_to_subgraph(source, subgraph, num_threads);

                // This should not be necessary, if the extraction works correctly
                // graph.remove_orphan_edges();
            };

            if (_split_subgraphs) {
                for (auto &target : targets) {
                    graph_t subgraph;

                    path_handle_t path_handle = graph.get_path_handle(target.seq);

                    if (debug) {
                        std::cerr << "[odgi::extract] extracting path range " << target.seq << ":" << target.start
                                  << "-"
                                  << target.end << std::endl;
                    }
                    algorithms::extract_path_range(graph, path_handle, target.start, target.end, subgraph);

                    if (_full_range) {
                        if (debug) {
                            std::cerr << "[odgi::extract] collecting all nodes in the path range" << std::endl;
                        }

                        // find the start and end node of this and fill things in
                        nid_t id_start = std::numeric_limits<nid_t>::max();
                        nid_t id_end = 1;
                        subgraph.for_each_handle([&](handle_t handle) {
                            nid_t id = subgraph.get_id(handle);
                            id_start = std::min(id_start, id);
                            id_end = std::max(id_end, id);
                        });

                        algorithms::extract_id_range(graph, id_start, id_end, subgraph);
                    }
                    prep_graph(graph, subgraph, context_size, _use_length);

                    string filename = target.seq + ":" + to_string(target.start) + "-" + to_string(target.end) + ".og";

                    if (debug) {
                        std::cerr << "[odgi::extract] writing " << filename << std::endl;
                    }
                    ofstream f(filename);
                    subgraph.serialize(f);
                    f.close();
                }
            } else {
                graph_t subgraphs;

                for (auto &target : targets) {
                    path_handle_t path_handle = graph.get_path_handle(target.seq);

                    if (debug) {
                        std::cerr << "[odgi::extract] extracting path range " << target.seq << ":" << target.start
                                  << "-"
                                  << target.end << std::endl;
                    }
                    algorithms::extract_path_range(graph, path_handle, target.start, target.end, subgraphs);
                }

                if (_full_range) {
                    if (debug) {
                        std::cerr << "[odgi::extract] collecting all nodes in the path range" << std::endl;
                    }

                    // find the start and end node of this and fill things in
                    nid_t id_start = std::numeric_limits<nid_t>::max();
                    nid_t id_end = 1;
                    subgraphs.for_each_handle([&](handle_t handle) {
                        nid_t id = subgraphs.get_id(handle);
                        id_start = std::min(id_start, id);
                        id_end = std::max(id_end, id);
                    });

                    algorithms::extract_id_range(graph, id_start, id_end, subgraphs);
                }
                prep_graph(graph, subgraphs, context_size, _use_length);

                std::string outfile = args::get(og_out_file);
                if (!outfile.empty()) {
                    if (outfile == "-") {
                        subgraphs.serialize(std::cout);
                    } else {
                        ofstream f(outfile.c_str());
                        subgraphs.serialize(f);
                        f.close();
                    }
                }
            }
        } else {
            graph_t extract;

            // collect the new graph
            if (args::get(_target_node)) {
                uint64_t node_id = args::get(_target_node);
                handle_t handle = graph.get_handle(node_id);
                extract.create_handle(graph.get_sequence(handle), node_id);
            }

            if (!args::get(_node_list).empty()) {
                ifstream nodes(args::get(_node_list));
                std::string next;
                while (std::getline(nodes, next)) {
                    uint64_t id = std::stol(next);
                    if (graph.has_node(id)) {
                        handle_t h = graph.get_handle(id);
                        extract.create_handle(graph.get_sequence(h), id);
                    } else {
                        std::cerr << "[odgi::extract] warning, cannot find node " << id << std::endl;
                    }
                }
            }

            if (args::get(_context_size) > 0) {
                uint64_t context = args::get(_context_size);
                for (uint64_t i = 0; i < context; ++i) {
                    // get the edges and connected nodes from the graph to fill out th subgraph
                    // for each extract node
                    // get its component in the old graph
                    // and add it to the extract
                    std::vector<handle_t> curr_handles;
                    extract.for_each_handle([&](const handle_t &h) {
                        curr_handles.push_back(h);
                    });
                    for (auto &h : curr_handles) {
                        handle_t old_h = graph.get_handle(extract.get_id(h));
                        graph.follow_edges(old_h, false, [&](const handle_t &c) {
                            handle_t x = extract.create_handle(graph.get_sequence(c), graph.get_id(c));
                            extract.create_edge(h, x);
                        });
                        graph.follow_edges(old_h, true, [&](const handle_t &c) {
                            handle_t x = extract.create_handle(graph.get_sequence(c), graph.get_id(c));
                            extract.create_edge(x, h);
                        });
                    }
                }
            }

            std::string outfile = args::get(og_out_file);
            if (!outfile.empty()) {
                if (outfile == "-") {
                    extract.serialize(std::cout);
                } else {
                    ofstream f(outfile.c_str());
                    extract.serialize(f);
                    f.close();
                }
            }
        }

        return 0;
    }

    static Subcommand odgi_extract("extract", "extract parts of the graph using paths, positions, and nodes",
                                   PIPELINE, 3, main_extract);

}
