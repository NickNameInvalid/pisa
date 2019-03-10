#include <CLI/CLI.hpp>
#include <mio/mmap.hpp>
#include <spdlog/spdlog.h>

#include "payload_vector.hpp"
#include "io.hpp"

using namespace pisa;

int main(int argc, char **argv)
{
    std::string text_file;
    std::string lexicon_file;
    std::size_t idx;
    std::string value;

    CLI::App app{"Build, print, or query lexicon"};
    app.require_subcommand();
    auto build = app.add_subcommand("build", "Build a lexicon");
    build->add_option("input", text_file, "Input text file")->required();
    build->add_option("output", lexicon_file, "Output file")->required();
    auto lookup = app.add_subcommand("lookup", "Retrieve the payload at index");
    lookup->add_option("lexicon", lexicon_file, "Lexicon file path")->required();
    lookup->add_option("idx", idx, "Index of requested element")->required();
    auto rlookup = app.add_subcommand("rlookup", "Retrieve the index of payload");
    rlookup->add_option("lexicon", lexicon_file, "Lexicon file path")->required();
    rlookup->add_option("value", value, "Requested value")->required();
    auto print = app.add_subcommand("print", "Print elements line by line");
    print->add_option("lexicon", lexicon_file, "Lexicon file path")->required();
    CLI11_PARSE(app, argc, argv);

    if (*build) {
        std::ifstream is(text_file);
        encode_payload_vector(std::istream_iterator<io::Line>(is),
                              std::istream_iterator<io::Line>())
            .to_file(lexicon_file);
        return 0;
    }
    mio::mmap_source m(lexicon_file.c_str());
    auto lexicon = Payload_Vector<>::from(m);
    if (*print) {
        for (auto const &elem : lexicon) {
            std::cout << elem << '\n';
        }
    } else if (*lookup) {
        if (idx < lexicon.size()) {
            std::cout << lexicon[idx] << '\n';
        } else {
            spdlog::error(
                "Requested index {} too large for vector of size {}", idx, lexicon.size());
        }
    } else if (*rlookup) {
        auto pos = std::lower_bound(lexicon.begin(), lexicon.end(), std::string_view(value));
        if (pos != lexicon.end() and *pos == std::string_view(value)) {
            std::cout << std::distance(lexicon.begin(), pos) << '\n';
        } else {
            spdlog::error("Requested term {} was not found", value);
        }
    }
    return 1;
}
