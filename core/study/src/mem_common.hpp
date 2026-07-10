/* SPDX-License-Identifier: MPL-2.0 */
/* Shared helpers for the connection-memory study binaries. */

#ifndef ZLINK_STUDY_MEM_COMMON_HPP
#define ZLINK_STUDY_MEM_COMMON_HPP

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <string>

#include <dirent.h>
#include <malloc.h>
#include <sys/resource.h>
#include <unistd.h>

namespace study
{
struct mem_stats_t
{
    size_t vmrss_kb = 0;
    size_t vmhwm_kb = 0;
    size_t anon_kb = 0;
    size_t priv_kb = 0; /* Private_Clean + Private_Dirty from smaps_rollup */
    int fds = 0;
};

inline size_t parse_status_kb (const char *line_)
{
    unsigned long value = 0;
    const char *colon = strchr (line_, ':');
    if (colon && sscanf (colon + 1, "%lu", &value) == 1)
        return static_cast<size_t> (value);
    return 0;
}

inline int count_open_fds ()
{
    DIR *dir = opendir ("/proc/self/fd");
    if (!dir)
        return -1;
    int count = 0;
    while (readdir (dir))
        ++count;
    closedir (dir);
    return count - 3; /* ".", "..", and the opendir fd itself */
}

inline mem_stats_t read_mem_stats ()
{
    mem_stats_t out;

    if (FILE *fp = fopen ("/proc/self/status", "r")) {
        char line[256];
        while (fgets (line, sizeof (line), fp)) {
            if (!strncmp (line, "VmRSS:", 6))
                out.vmrss_kb = parse_status_kb (line);
            else if (!strncmp (line, "VmHWM:", 6))
                out.vmhwm_kb = parse_status_kb (line);
        }
        fclose (fp);
    }

    if (FILE *fp = fopen ("/proc/self/smaps_rollup", "r")) {
        char line[256];
        while (fgets (line, sizeof (line), fp)) {
            if (!strncmp (line, "Anonymous:", 10))
                out.anon_kb = parse_status_kb (line);
            else if (!strncmp (line, "Private_Clean:", 14) || !strncmp (line, "Private_Dirty:", 14))
                out.priv_kb += parse_status_kb (line);
        }
        fclose (fp);
    }

    out.fds = count_open_fds ();
    return out;
}

inline void print_rss (const char *label_)
{
    const mem_stats_t m = read_mem_stats ();
    printf ("RSS %s vmrss_kb=%zu vmhwm_kb=%zu anon_kb=%zu priv_kb=%zu fds=%d\n", label_, m.vmrss_kb,
            m.vmhwm_kb, m.anon_kb, m.priv_kb, m.fds);
    fflush (stdout);
}

inline void raise_nofile_limit ()
{
    struct rlimit lim;
    if (getrlimit (RLIMIT_NOFILE, &lim) != 0)
        return;
    lim.rlim_cur = lim.rlim_max;
    setrlimit (RLIMIT_NOFILE, &lim);
}

/* Reads commands from stdin, one per line:
 *   rss <label>  -> print memory stats
 *   trim         -> malloc_trim(0), then print stats with label "trim"
 *   quit         -> return
 *   anything else -> dispatched through handlers_ (first token = key)
 */
inline void
run_command_loop (const std::map<std::string, std::function<void (const std::string &)>> &handlers_)
{
    char line[512];
    while (fgets (line, sizeof (line), stdin)) {
        std::string cmd (line);
        while (!cmd.empty () && (cmd.back () == '\n' || cmd.back () == '\r'))
            cmd.pop_back ();
        if (cmd.empty ())
            continue;

        const size_t space = cmd.find (' ');
        const std::string head = cmd.substr (0, space);
        const std::string arg = space == std::string::npos ? std::string () : cmd.substr (space + 1);

        if (head == "quit")
            return;
        if (head == "rss") {
            print_rss (arg.empty () ? "unnamed" : arg.c_str ());
            continue;
        }
        if (head == "trim") {
            malloc_trim (0);
            printf ("TRIMMED\n");
            fflush (stdout);
            continue;
        }

        const auto it = handlers_.find (head);
        if (it != handlers_.end ()) {
            it->second (arg);
        } else {
            printf ("ERR unknown command: %s\n", head.c_str ());
            fflush (stdout);
        }
    }
}

} // namespace study

#endif
