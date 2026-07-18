#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "telemux/checksum.h"
#include "telemux/codec_rle.h"
#include "telemux/frame_parser.h"
#include "telemux/metrics.h"
#include "telemux/query.h"
#include "telemux/reassembly.h"
#include "telemux/recv_arena.h"
#include "telemux/serializer.h"
#include "telemux/stats_export.h"
#include "telemux/tlmx_file.h"

namespace {

std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
}

int cmd_decode(const std::string& path) {
    std::vector<uint8_t> input = read_file(path);
    telemux::TelemuxConfig config;
    telemux::RecvArena arena(config.recv_arena_initial_bytes);
    telemux::ReassemblyTracker reassembly;
    telemux::FrameParser parser(arena, reassembly, config);

    auto result = parser.feed_frame(input.data(), input.size());
    if (!result.ok()) {
        std::fprintf(stderr, "decode failed: %s\n", telemux::error_code_name(result.error().code));
        return 1;
    }
    std::printf("decoded %zu bytes\n", result.value().size());
    telemux::MetricsRegistry::instance().increment_counter("cli.decode.ok");
    return 0;
}

int cmd_stats() {
    std::string json = telemux::render_stats_json(telemux::MetricsRegistry::instance());
    std::printf("%s\n", json.c_str());
    return 0;
}

int cmd_query(const std::string& expr) {
    auto tokens = telemux::lex_query(expr);
    auto parsed = telemux::parse_query(tokens);
    if (!parsed.ok()) {
        std::fprintf(stderr, "query parse error: %s\n", parsed.error().detail.c_str());
        return 1;
    }
    telemux::QueryContext ctx{1, 3, 150};
    bool matches = telemux::eval_query(*parsed.value(), ctx);
    std::printf("matches: %s\n", matches ? "true" : "false");
    return 0;
}

int cmd_inspect(const std::string& path) {
    auto reader = telemux::TlmxReader::open_file(path);
    if (!reader.ok()) {
        std::fprintf(stderr, "inspect failed: %s\n", telemux::error_code_name(reader.error().code));
        return 1;
    }

    const auto& index = reader.value().index();
    std::printf("container version %u, %zu session(s)\n", reader.value().version(), index.size());
    for (const auto& entry : index) {
        std::printf("  session %u: %u record(s) across %zu page(s)\n", entry.session_id,
                     entry.total_records, entry.page_offsets.size());
    }
    return 0;
}

void print_usage() {
    std::fprintf(stderr,
                  "usage: telemux_cli <decode|inspect|stats|query> [args]\n"
                  "  decode <file>       decode a single frame from a file\n"
                  "  inspect <file>      summarize a .tlmx session container\n"
                  "  stats               print current metrics as JSON\n"
                  "  query <expr>        evaluate a filter expression\n");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string cmd = argv[1];
    if (cmd == "decode" && argc >= 3) {
        return cmd_decode(argv[2]);
    }
    if (cmd == "inspect" && argc >= 3) {
        return cmd_inspect(argv[2]);
    }
    if (cmd == "stats") {
        return cmd_stats();
    }
    if (cmd == "query" && argc >= 3) {
        return cmd_query(argv[2]);
    }

    print_usage();
    return 1;
}
