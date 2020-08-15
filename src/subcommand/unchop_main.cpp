#include "subcommand.hpp"
#include "odgi.hpp"
#include "args.hxx"
#include "threads.hpp"
#include "algorithms/unchop.hpp"

namespace odgi {

using namespace odgi::subcommand;

int main_unchop(int argc, char** argv) {

    // trick argumentparser to do the right thing with the subcommand
    for (uint64_t i = 1; i < argc-1; ++i) {
        argv[i] = argv[i+1];
    }
    std::string prog_name = "odgi unchop";
    argv[0] = (char*)prog_name.c_str();
    --argc;
    
    args::ArgumentParser parser("merge unitigs into single nodes");
    args::HelpFlag help(parser, "help", "display this help summary", {'h', "help"});
    args::ValueFlag<std::string> og_in_file(parser, "FILE", "load the graph from this file", {'i', "idx"});
    args::ValueFlag<std::string> og_out_file(parser, "FILE", "store the graph self index in this file", {'o', "out"});
    //args::ValueFlag<uint64_t> max_iterations(parser, "FILE", "iterate normalization up to this many times (default: 10)", {'I', "max-iterations"});
    //args::Flag debug(parser, "debug", "print information about the normalization process", {'d', "debug"});
    //args::ValueFlag<uint64_t> threads(parser, "N", "number of threads to use", {'t', "threads"});

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
    if (argc==1) {
        std::cout << parser;
        return 1;
    }

    if (!og_in_file) {
        std::cerr << "[odgi unchop] error: Please specify an input file from where to load the graph via -i=[FILE], --idx=[FILE]." << std::endl;
        return 1;
    }

    if (!og_out_file) {
        std::cerr << "[odgi unchop] error: Please specify an output file to where to store the unchopped graph via -o=[FILE], --out=[FILE]." << std::endl;
        return 1;
    }

    graph_t graph;
    assert(argc > 0);
    std::string infile = args::get(og_in_file);
    if (infile.size()) {
        if (infile == "-") {
            graph.deserialize(std::cin);
        } else {
            ifstream f(infile.c_str());
            graph.deserialize(f);
            f.close();
        }
    }
    /*
    if (args::get(threads)) {
        omp_set_num_threads(args::get(threads));
    }
    */
    algorithms::unchop(graph);
    graph.optimize();
    
    std::string outfile = args::get(og_out_file);
    if (outfile.size()) {
        if (outfile == "-") {
            graph.serialize(std::cout);
        } else {
            ofstream f(outfile.c_str());
            graph.serialize(f);
            f.close();
        }
    }
    return 0;
}

static Subcommand odgi_unchop("unchop", "merge unitigs into single nodes",
                              PIPELINE, 3, main_unchop);


}
