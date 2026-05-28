#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
std::string read_file(const std::filesystem::path& path)
{
    std::ifstream in(path);
    assert(in.good());
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

void collect_files(const std::filesystem::path& dir,
                   std::vector<std::filesystem::path>& out)
{
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file())
            continue;
        const auto ext = entry.path().extension().string();
        if (ext == ".cpp" || ext == ".hpp" || ext == ".h")
            out.push_back(entry.path());
    }
}

bool contains_aggregate_call(const std::string& body, const std::string& symbol)
{
    const std::string needle = symbol + " (";
    std::size_t pos = 0;
    while ((pos = body.find(needle, pos)) != std::string::npos) {
        if (body.compare(pos, symbol.size() + 6, symbol + "_part") != 0)
            return true;
        pos += needle.size();
    }

    const std::string compact_needle = symbol + "(";
    pos = 0;
    while ((pos = body.find(compact_needle, pos)) != std::string::npos) {
        if (body.compare(pos, symbol.size() + 5, symbol + "_part") != 0)
            return true;
        pos += compact_needle.size();
    }
    return false;
}
}

int main()
{
    const std::filesystem::path this_file = __FILE__;
    const std::filesystem::path cpp_root =
      this_file.parent_path().parent_path().parent_path();

    std::vector<std::filesystem::path> files;
    collect_files(cpp_root / "src" / "Runtime", files);

    std::string all;
    for (const auto& file : files)
        all += read_file(file) + "\n";

    const std::vector<std::string> required_part_symbols = {
      "zlink_send_part",
      "zlink_recv_part",
      "zlink_publish_part",
      "zlink_subscribe_part",
      "zlink_router_recv_part",
      "zlink_dealer_request_part",
      "zlink_router_request_part",
      "zlink_router_reply_part",
      "zlink_spot_publish_part",
      "zlink_spot_subscribe_part",
      "zlink_spot_request_channel_part",
      "zlink_spot_request_spot_part",
      "zlink_spot_reply_router_part",
    };
    for (const auto& symbol : required_part_symbols)
        assert(all.find(symbol) != std::string::npos);

    const std::vector<std::string> aggregate_symbols = {
      "zlink_send",
      "zlink_recv",
      "zlink_publish",
      "zlink_subscribe",
      "zlink_router_recv",
      "zlink_dealer_request",
      "zlink_router_request",
      "zlink_router_reply",
      "zlink_spot_send_channel",
      "zlink_spot_request_channel",
      "zlink_spot_request_spot",
      "zlink_spot_request_router",
      "zlink_spot_publish",
      "zlink_spot_subscribe",
      "zlink_spot_send_spot",
      "zlink_spot_reply_spot",
      "zlink_spot_reply_router",
      "zlink_spot_recv",
    };

    for (const auto& symbol : aggregate_symbols)
        assert(!contains_aggregate_call(all, symbol));

    assert(all.find("std::this_thread::sleep_for") == std::string::npos);
    return 0;
}
