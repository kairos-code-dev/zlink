#ifndef PERF_SINGLE_TLS_HPP
#define PERF_SINGLE_TLS_HPP

#include "../../common/perf_socket_compat.hpp"

#include <filesystem>
#include <string>
#include <vector>
#include <iostream>

namespace perf {
namespace single {

inline bool file_exists (const std::filesystem::path &path)
{
    std::error_code ec;
    return std::filesystem::exists (path, ec)
           && std::filesystem::is_regular_file (path, ec);
}

inline bool resolve_tls_dir_from (const std::filesystem::path &start,
                                  std::filesystem::path *out_dir)
{
    if (!out_dir)
        return false;

    std::error_code ec;
    std::filesystem::path cur = start;
    if (cur.empty ())
        return false;

    if (std::filesystem::is_regular_file (cur, ec))
        cur = cur.parent_path ();

    for (int i = 0; i < 16; ++i) {
        if (cur.empty ())
            break;

        const std::filesystem::path candidate =
          cur / "bindings" / "cpp" / "tests" / "certs" / "gen";
        const std::filesystem::path cert = candidate / "server.crt";
        const std::filesystem::path key = candidate / "server.key";
        const std::filesystem::path ca = candidate / "ca.crt";
        if (file_exists (cert) && file_exists (key) && file_exists (ca)) {
            *out_dir = candidate;
            return true;
        }

        const std::filesystem::path alt = cur / "tests" / "certs" / "gen";
        const std::filesystem::path alt_cert = alt / "server.crt";
        const std::filesystem::path alt_key = alt / "server.key";
        const std::filesystem::path alt_ca = alt / "ca.crt";
        if (file_exists (alt_cert) && file_exists (alt_key)
            && file_exists (alt_ca)) {
            *out_dir = alt;
            return true;
        }

        const std::filesystem::path parent = cur.parent_path ();
        if (parent == cur)
            break;
        cur = parent;
    }

    return false;
}

inline bool try_resolve_perf_tls_paths (std::string &cert_out,
                                        std::string &key_out,
                                        std::string &ca_out)
{
    cert_out.clear ();
    key_out.clear ();
    ca_out.clear ();

    std::filesystem::path dir;
    const std::filesystem::path cwd = std::filesystem::current_path ();
    if (!resolve_tls_dir_from (cwd, &dir)) {
        const std::filesystem::path exe_probe =
          std::filesystem::path ("/proc/self/exe");
        std::error_code ec;
        const std::filesystem::path exe =
          std::filesystem::read_symlink (exe_probe, ec);
        if (ec || !resolve_tls_dir_from (exe, &dir))
            return false;
    }

    cert_out = (dir / "server.crt").string ();
    key_out = (dir / "server.key").string ();
    ca_out = (dir / "ca.crt").string ();
    return true;
}

inline bool setup_tls_server (zlink::socket_t &socket,
                              const std::string &transport)
{
    if (transport != "tls" && transport != "wss")
        return true;

    std::string cert;
    std::string key;
    std::string ca;
    if (!try_resolve_perf_tls_paths (cert, key, ca)) {
        std::cerr << "TLS cert path not found: bindings/cpp/tests/certs/gen"
                  << std::endl;
        return false;
    }

    if (socket.set (zlink::socket_options::tls_cert, cert) != 0)
        return false;
    if (socket.set (zlink::socket_options::tls_key, key) != 0)
        return false;
    return true;
}

inline bool setup_tls_client (zlink::socket_t &socket,
                              const std::string &transport)
{
    if (transport != "tls" && transport != "wss")
        return true;

    std::string cert;
    std::string key;
    std::string ca;
    if (!try_resolve_perf_tls_paths (cert, key, ca)) {
        std::cerr << "TLS cert path not found: bindings/cpp/tests/certs/gen"
                  << std::endl;
        return false;
    }

    if (socket.set (zlink::socket_options::tls_ca, ca) != 0)
        return false;
    if (socket.set (zlink::socket_options::tls_hostname,
                    std::string ("localhost"))
        != 0) {
        return false;
    }
    if (socket.set (zlink::socket_options::tls_trust_system, 0) != 0)
        return false;
    return true;
}

} // namespace single
} // namespace perf

#endif
