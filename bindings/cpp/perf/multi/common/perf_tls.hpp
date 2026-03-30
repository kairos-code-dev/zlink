#ifndef PERF_MULTI_TLS_HPP
#define PERF_MULTI_TLS_HPP

#include "../../common/perf_socket_compat.hpp"

#include <filesystem>
#include <string>

namespace perf {
namespace multi {

typedef zlink::socket_t perf_socket_t;

inline bool tls_file_exists (const std::filesystem::path &path)
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
        if (tls_file_exists (candidate / "server.crt")
            && tls_file_exists (candidate / "server.key")
            && tls_file_exists (candidate / "ca.crt")) {
            *out_dir = candidate;
            return true;
        }

        const std::filesystem::path alt = cur / "tests" / "certs" / "gen";
        if (tls_file_exists (alt / "server.crt")
            && tls_file_exists (alt / "server.key")
            && tls_file_exists (alt / "ca.crt")) {
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
    if (!resolve_tls_dir_from (std::filesystem::current_path (), &dir)) {
        const std::filesystem::path exe_probe = std::filesystem::path ("/proc/self/exe");
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

inline bool setup_tls_server (perf_socket_t &socket,
                              const std::string &transport)
{
    if (transport != "tls" && transport != "wss")
        return true;

    std::string cert;
    std::string key;
    std::string ca;
    if (!try_resolve_perf_tls_paths (cert, key, ca))
        return false;

    return socket.set_option (zlink::socket_options::tls_cert, cert) == 0
           && socket.set_option (zlink::socket_options::tls_key, key) == 0;
}

inline bool setup_tls_client (perf_socket_t &socket,
                              const std::string &transport)
{
    if (transport != "tls" && transport != "wss")
        return true;

    std::string cert;
    std::string key;
    std::string ca;
    if (!try_resolve_perf_tls_paths (cert, key, ca))
        return false;

    return socket.set_option (zlink::socket_options::tls_ca, ca) == 0
           && socket.set_option (zlink::socket_options::tls_hostname,
                                 std::string ("localhost"))
                == 0
           && socket.set_option (zlink::socket_options::tls_trust_system, 0)
                == 0;
}

} // namespace multi
} // namespace perf

#endif
