/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#ifndef _WIN32

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <climits>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern char **environ;

namespace
{
const char *g_self_path = NULL;

bool should_run_spot_process_test (const char *name_)
{
    const char *selected = getenv ("ZLINK_TEST_CASE");
    return !selected || !*selected || strcmp (selected, name_) == 0;
}

struct process_capture_t
{
    process_capture_t () :
        pid (-1),
        stdin_fd (-1),
        stdout_file (NULL),
        stderr_file (NULL),
        stdout_done (false),
        stderr_done (false)
    {
    }

    pid_t pid;
    int stdin_fd;
    FILE *stdout_file;
    FILE *stderr_file;
    std::thread stdout_thread;
    std::thread stderr_thread;
    std::mutex sync;
    std::condition_variable cv;
    std::vector<std::string> stdout_lines;
    std::string stderr_text;
    bool stdout_done;
    bool stderr_done;
};

void cleanup_child (process_capture_t *proc_);

process_capture_t *g_active_processes[8] = {NULL};

process_capture_t *create_process_capture ()
{
    return new process_capture_t ();
}

void unregister_active_process (process_capture_t *proc_)
{
    if (!proc_)
        return;

    for (size_t i = 0; i < sizeof (g_active_processes) / sizeof (*g_active_processes); ++i) {
        if (g_active_processes[i] == proc_)
            g_active_processes[i] = NULL;
    }
}

void register_active_process (process_capture_t *proc_)
{
    if (!proc_)
        return;

    unregister_active_process (proc_);
    for (size_t i = 0; i < sizeof (g_active_processes) / sizeof (*g_active_processes); ++i) {
        if (!g_active_processes[i]) {
            g_active_processes[i] = proc_;
            return;
        }
    }
    TEST_FAIL_MESSAGE ("active process registry overflow");
}

void destroy_process_capture (process_capture_t *proc_)
{
    if (!proc_)
        return;

    unregister_active_process (proc_);
    cleanup_child (proc_);
    delete proc_;
}

void cleanup_active_processes ()
{
    for (size_t i = 0; i < sizeof (g_active_processes) / sizeof (*g_active_processes); ++i) {
        process_capture_t *proc = g_active_processes[i];
        if (!proc)
            continue;
        g_active_processes[i] = NULL;
        cleanup_child (proc);
        delete proc;
    }
}

std::string sibling_binary_path (const char *argv0_, const char *name_)
{
    std::string path = argv0_ ? argv0_ : "";
    const std::string::size_type slash = path.find_last_of ('/');
    if (slash == std::string::npos)
        return std::string (name_);
    return path.substr (0, slash + 1) + name_;
}

bool is_secure_transport_name (const char *transport_)
{
    return transport_
           && (std::strcmp (transport_, "tls") == 0 || std::strcmp (transport_, "wss") == 0);
}

size_t max_message_size_from_csv (const char *msg_sizes_)
{
    if (!msg_sizes_)
        return 0;

    size_t max_size = 0;
    const char *cursor = msg_sizes_;
    while (*cursor) {
        char *end = NULL;
        const unsigned long long parsed = std::strtoull (cursor, &end, 10);
        if (end == cursor)
            break;
        if (parsed > max_size)
            max_size = static_cast<size_t> (parsed);
        cursor = *end == ',' ? end + 1 : end;
        if (*cursor == '\0')
            break;
    }

    return max_size;
}

int resolve_case_exit_timeout_ms (const char *transport_,
                                  const char *msg_sizes_,
                                  int clients_,
                                  int duration_seconds_,
                                  int warmup_seconds_)
{
    int timeout_ms = std::max (30000, (duration_seconds_ + warmup_seconds_ + 10) * 1000);
    const size_t max_msg_size = max_message_size_from_csv (msg_sizes_);

    if (clients_ >= 100 || max_msg_size >= 131072)
        timeout_ms = std::max (timeout_ms, 60000);
    if (is_secure_transport_name (transport_))
        timeout_ms = std::max (timeout_ms, 60000);
    if (is_secure_transport_name (transport_) && max_msg_size >= 131072)
        timeout_ms = std::max (timeout_ms, 120000);
    if (is_secure_transport_name (transport_) && max_msg_size >= 262144)
        timeout_ms = std::max (timeout_ms, 150000);

    return timeout_ms;
}

void close_process_capture (process_capture_t *proc_)
{
    if (!proc_)
        return;

    if (proc_->stdin_fd >= 0) {
        close (proc_->stdin_fd);
        proc_->stdin_fd = -1;
    }
    if (proc_->stdout_thread.joinable ())
        proc_->stdout_thread.join ();
    if (proc_->stderr_thread.joinable ())
        proc_->stderr_thread.join ();
    if (proc_->stdout_file) {
        fclose (proc_->stdout_file);
        proc_->stdout_file = NULL;
    }
    if (proc_->stderr_file) {
        fclose (proc_->stderr_file);
        proc_->stderr_file = NULL;
    }
}

void cleanup_child (process_capture_t *proc_)
{
    if (!proc_ || proc_->pid <= 0)
        return;

    int status = 0;
    const pid_t waited = waitpid (proc_->pid, &status, WNOHANG);
    if (waited == 0) {
        kill (proc_->pid, SIGKILL);
        waitpid (proc_->pid, &status, 0);
    }
    proc_->pid = -1;
    close_process_capture (proc_);
}

void reader_loop (FILE *stream_, process_capture_t *proc_, bool stdout_stream_)
{
    char buffer[512];
    while (stream_ && fgets (buffer, sizeof (buffer), stream_)) {
        std::lock_guard<std::mutex> lock (proc_->sync);
        if (stdout_stream_)
            proc_->stdout_lines.push_back (std::string (buffer));
        else
            proc_->stderr_text.append (buffer);
        proc_->cv.notify_all ();
    }

    std::lock_guard<std::mutex> lock (proc_->sync);
    if (stdout_stream_)
        proc_->stdout_done = true;
    else
        proc_->stderr_done = true;
    proc_->cv.notify_all ();
}

bool start_process (const std::string &path_,
                    const std::vector<std::string> &args_,
                    const std::vector<std::string> &env_overrides_,
                    process_capture_t *out_)
{
    if (!out_)
        return false;

    int stdin_pipe[2] = {-1, -1};
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    if (pipe (stdin_pipe) != 0 || pipe (stdout_pipe) != 0 || pipe (stderr_pipe) != 0) {
        return false;
    }

    const pid_t pid = fork ();
    if (pid < 0) {
        close (stdin_pipe[0]);
        close (stdin_pipe[1]);
        close (stdout_pipe[0]);
        close (stdout_pipe[1]);
        close (stderr_pipe[0]);
        close (stderr_pipe[1]);
        return false;
    }

    if (pid == 0) {
        dup2 (stdin_pipe[0], STDIN_FILENO);
        dup2 (stdout_pipe[1], STDOUT_FILENO);
        dup2 (stderr_pipe[1], STDERR_FILENO);

        close (stdin_pipe[0]);
        close (stdin_pipe[1]);
        close (stdout_pipe[0]);
        close (stdout_pipe[1]);
        close (stderr_pipe[0]);
        close (stderr_pipe[1]);

        std::vector<std::string> env_storage;
        for (char **it = environ; it && *it; ++it)
            env_storage.push_back (*it);
        env_storage.insert (env_storage.end (), env_overrides_.begin (), env_overrides_.end ());

        std::vector<char *> envp;
        for (size_t i = 0; i < env_storage.size (); ++i)
            envp.push_back (const_cast<char *> (env_storage[i].c_str ()));
        envp.push_back (NULL);

        std::vector<char *> argv;
        argv.push_back (const_cast<char *> (path_.c_str ()));
        for (size_t i = 0; i < args_.size (); ++i)
            argv.push_back (const_cast<char *> (args_[i].c_str ()));
        argv.push_back (NULL);

        execve (path_.c_str (), &argv[0], &envp[0]);
        _exit (127);
    }

    close (stdin_pipe[0]);
    close (stdout_pipe[1]);
    close (stderr_pipe[1]);

    out_->pid = pid;
    out_->stdin_fd = stdin_pipe[1];
    out_->stdout_file = fdopen (stdout_pipe[0], "r");
    out_->stderr_file = fdopen (stderr_pipe[0], "r");
    if (!out_->stdout_file || !out_->stderr_file) {
        cleanup_child (out_);
        return false;
    }

    out_->stdout_thread = std::thread (&reader_loop, out_->stdout_file, out_, true);
    out_->stderr_thread = std::thread (&reader_loop, out_->stderr_file, out_, false);
    return true;
}

bool wait_for_stdout_prefix (process_capture_t *proc_,
                             const char *prefix_,
                             int timeout_ms,
                             std::string *line_out_)
{
    if (!proc_ || !prefix_)
        return false;

    std::unique_lock<std::mutex> lock (proc_->sync);
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms > 0 ? timeout_ms : 1);

    while (std::chrono::steady_clock::now () < deadline) {
        for (size_t i = 0; i < proc_->stdout_lines.size (); ++i) {
            if (proc_->stdout_lines[i].compare (0, std::strlen (prefix_), prefix_) == 0) {
                if (line_out_)
                    *line_out_ = proc_->stdout_lines[i];
                return true;
            }
        }
        if (proc_->stdout_done)
            return false;
        proc_->cv.wait_until (lock, deadline);
    }

    return false;
}

std::string capture_case_debug_text (process_capture_t *server_, process_capture_t *client_);
void write_stdin_line (process_capture_t *proc_, const char *line_);

bool collect_stdout_prefix_lines (process_capture_t *proc_,
                                  const char *prefix_,
                                  size_t expected_count_,
                                  int timeout_ms,
                                  std::vector<std::string> *lines_out_)
{
    if (!proc_ || !prefix_ || expected_count_ == 0)
        return false;

    std::unique_lock<std::mutex> lock (proc_->sync);
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms > 0 ? timeout_ms : 1);

    while (std::chrono::steady_clock::now () < deadline) {
        std::vector<std::string> matches;
        for (size_t i = 0; i < proc_->stdout_lines.size (); ++i) {
            if (proc_->stdout_lines[i].compare (0, std::strlen (prefix_), prefix_) == 0) {
                matches.push_back (proc_->stdout_lines[i]);
            }
        }
        if (matches.size () >= expected_count_) {
            if (lines_out_)
                *lines_out_ = matches;
            return true;
        }
        if (proc_->stdout_done)
            return false;
        proc_->cv.wait_until (lock, deadline);
    }

    return false;
}

std::string parse_client_endpoint_line (const std::string &line_)
{
    const std::string prefix = "CLIENT_CONTROL_ENDPOINT,";
    if (line_.compare (0, prefix.size (), prefix) != 0)
        return std::string ();

    std::string endpoint = line_.substr (prefix.size ());
    if (!endpoint.empty () && endpoint[endpoint.size () - 1] == '\n')
        endpoint.erase (endpoint.size () - 1);
    return endpoint;
}

void forward_client_spot_endpoints (process_capture_t *server_,
                                    process_capture_t *client_,
                                    int timeout_ms)
{
    TEST_ASSERT_NOT_NULL (server_);
    TEST_ASSERT_NOT_NULL (client_);

    std::vector<std::string> endpoint_lines;
    TEST_ASSERT_TRUE_MESSAGE (collect_stdout_prefix_lines (client_, "CLIENT_CONTROL_ENDPOINT,", 1,
                                                           timeout_ms, &endpoint_lines),
                              capture_case_debug_text (server_, client_).c_str ());
    const std::string endpoint = parse_client_endpoint_line (endpoint_lines[0]);
    TEST_ASSERT_FALSE_MESSAGE (endpoint.empty (),
                               capture_case_debug_text (server_, client_).c_str ());
    write_stdin_line (server_, (std::string ("CONNECT_CONTROL,") + endpoint + "\n").c_str ());
    std::string control_connected_line;
    TEST_ASSERT_TRUE_MESSAGE (
      wait_for_stdout_prefix (server_, "CONTROL_CONNECTED,", timeout_ms, &control_connected_line),
      capture_case_debug_text (server_, client_).c_str ());
    write_stdin_line (client_, control_connected_line.c_str ());
}

bool wait_for_exit_code (process_capture_t *proc_, int timeout_ms, int *rc_out_)
{
    if (!proc_ || proc_->pid <= 0)
        return false;

    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms > 0 ? timeout_ms : 1);

    int status = 0;
    while (std::chrono::steady_clock::now () < deadline) {
        const pid_t waited = waitpid (proc_->pid, &status, WNOHANG);
        if (waited == proc_->pid) {
            proc_->pid = -1;
            if (rc_out_) {
                if (WIFEXITED (status))
                    *rc_out_ = WEXITSTATUS (status);
                else if (WIFSIGNALED (status))
                    *rc_out_ = -WTERMSIG (status);
                else
                    *rc_out_ = -1;
            }
            return true;
        }
        msleep (10);
    }

    return false;
}

std::string capture_debug_text (process_capture_t *proc_)
{
    if (!proc_)
        return std::string ();

    std::lock_guard<std::mutex> lock (proc_->sync);
    std::string text;
    text.reserve (proc_->stderr_text.size () + 256);
    if (!proc_->stderr_text.empty ()) {
        text += "stderr:\n";
        text += proc_->stderr_text;
    }
    if (!proc_->stdout_lines.empty ()) {
        if (!text.empty ())
            text += '\n';
        text += "stdout:\n";
        for (size_t i = 0; i < proc_->stdout_lines.size (); ++i)
            text += proc_->stdout_lines[i];
    }
    return text;
}

std::string capture_case_debug_text (process_capture_t *server_, process_capture_t *client_)
{
    std::string text;
    const std::string server_text = capture_debug_text (server_);
    const std::string client_text = capture_debug_text (client_);

    if (!server_text.empty ()) {
        text += "server:\n";
        text += server_text;
    }
    if (!client_text.empty ()) {
        if (!text.empty ())
            text += '\n';
        text += "client:\n";
        text += client_text;
    }

    return text;
}

void write_stdin_line (process_capture_t *proc_, const char *line_)
{
    TEST_ASSERT_NOT_NULL (proc_);
    TEST_ASSERT_NOT_NULL (line_);
    TEST_ASSERT_TRUE (proc_->stdin_fd >= 0);
    const size_t size = std::strlen (line_);
    TEST_ASSERT_EQUAL_INT (static_cast<int> (size),
                           static_cast<int> (write (proc_->stdin_fd, line_, size)));
}

void stop_server_process (process_capture_t *proc_, int timeout_ms)
{
    if (!proc_ || proc_->pid <= 0)
        return;

    if (proc_->stdin_fd >= 0) {
        const char stop_line[] = "STOP\n";
        if (write (proc_->stdin_fd, stop_line, sizeof (stop_line) - 1) < 0) {
            // Best-effort graceful stop. Fallback cleanup still kills/waits.
        }
    }

    int server_rc = INT_MIN;
    if (wait_for_exit_code (proc_, timeout_ms, &server_rc))
        close_process_capture (proc_);
}
} // namespace

void run_multi_spot_process_case (const char *recv_mode_,
                                  const char *transport_,
                                  const char *msg_size_,
                                  int clients_,
                                  int connect_ready_timeout_ms_,
                                  const char *server_binary_,
                                  const char *client_binary_,
                                  const std::vector<std::string> *extra_env_,
                                  int duration_seconds_ = 1,
                                  int warmup_seconds_ = 1)
{
    process_capture_t *server = create_process_capture ();
    process_capture_t *client = create_process_capture ();
    register_active_process (server);
    register_active_process (client);

    const std::string server_path = sibling_binary_path (g_self_path, server_binary_);
    const std::string client_path = sibling_binary_path (g_self_path, client_binary_);

    std::vector<std::string> common_env;
    common_env.push_back (std::string ("PERF_MSG_SIZES=") + msg_size_);
    {
        char buf[32];
        snprintf (buf, sizeof (buf), "PERF_DURATION_SECONDS=%d", duration_seconds_);
        common_env.push_back (buf);
    }
    {
        char buf[32];
        snprintf (buf, sizeof (buf), "PERF_WARMUP_SECONDS=%d", warmup_seconds_);
        common_env.push_back (buf);
    }
    common_env.push_back ("PERF_SETTLE_MS=500");
    {
        char buf[32];
        snprintf (buf, sizeof (buf), "PERF_CLIENTS=%d", clients_);
        common_env.push_back (buf);
    }
    {
        char buf[64];
        snprintf (buf, sizeof (buf), "PERF_CONNECT_READY_TIMEOUT_MS=%d", connect_ready_timeout_ms_);
        common_env.push_back (buf);
    }
    if (clients_ >= 1000) {
        char buf[32];
        snprintf (buf, sizeof (buf), "PERF_MAX_SOCKETS=%d", 20000);
        common_env.push_back (buf);
    }
    common_env.push_back (std::string ("PERF_RECV_MODE=") + recv_mode_);
    if (extra_env_)
        common_env.insert (common_env.end (), extra_env_->begin (), extra_env_->end ());

    std::vector<std::string> server_args;
    server_args.push_back ("zlink");
    server_args.push_back (transport_);
    TEST_ASSERT_TRUE (start_process (server_path, server_args, common_env, server));

    std::string ready_line;
    TEST_ASSERT_TRUE_MESSAGE (wait_for_stdout_prefix (server, "READY,", 10000, &ready_line),
                              capture_case_debug_text (server, client).c_str ());
    std::string endpoint = ready_line.substr (strlen ("READY,"));
    if (!endpoint.empty () && endpoint[endpoint.size () - 1] == '\n')
        endpoint.erase (endpoint.size () - 1);
    std::string control_ready_line;
    TEST_ASSERT_TRUE_MESSAGE (
      wait_for_stdout_prefix (server, "CONTROL_READY,", 10000, &control_ready_line),
      capture_case_debug_text (server, client).c_str ());
    std::string control_endpoint = control_ready_line.substr (strlen ("CONTROL_READY,"));
    if (!control_endpoint.empty () && control_endpoint[control_endpoint.size () - 1] == '\n')
        control_endpoint.erase (control_endpoint.size () - 1);

    std::vector<std::string> client_args;
    client_args.push_back ("zlink");
    client_args.push_back (transport_);
    client_args.push_back (msg_size_);
    client_args.push_back ("--endpoint");
    client_args.push_back (endpoint);
    client_args.push_back ("--control-endpoint");
    client_args.push_back (control_endpoint);

    TEST_ASSERT_TRUE (start_process (client_path, client_args, common_env, client));

    forward_client_spot_endpoints (server, client, 20000);

    const std::string ready_prefix = std::string ("CLIENT_READY,") + msg_size_;
    TEST_ASSERT_TRUE_MESSAGE (wait_for_stdout_prefix (client, ready_prefix.c_str (), 20000, NULL),
                              capture_case_debug_text (server, client).c_str ());
    write_stdin_line (server, (std::string ("START,") + msg_size_ + "\n").c_str ());
    write_stdin_line (client, (std::string ("START,") + msg_size_ + "\n").c_str ());

    int client_rc = INT_MIN;
    const int exit_timeout_ms = resolve_case_exit_timeout_ms (transport_, msg_size_, clients_,
                                                              duration_seconds_, warmup_seconds_);
    TEST_ASSERT_TRUE_MESSAGE (wait_for_exit_code (client, exit_timeout_ms, &client_rc),
                              capture_case_debug_text (server, client).c_str ());
    close_process_capture (client);
    TEST_ASSERT_EQUAL_INT_MESSAGE (0, client_rc, capture_case_debug_text (server, client).c_str ());

    bool saw_throughput = false;
    {
        std::lock_guard<std::mutex> lock (client->sync);
        for (size_t i = 0; i < client->stdout_lines.size (); ++i) {
            if (client->stdout_lines[i].find ("throughput") != std::string::npos) {
                saw_throughput = true;
                break;
            }
        }
    }
    TEST_ASSERT_TRUE (saw_throughput);

    destroy_process_capture (client);
    stop_server_process (server, exit_timeout_ms);
    destroy_process_capture (server);
}

void run_multi_spot_process_case (const char *recv_mode_,
                                  const char *transport_,
                                  const char *msg_size_,
                                  int clients_,
                                  int connect_ready_timeout_ms_,
                                  int duration_seconds_ = 1,
                                  int warmup_seconds_ = 1)
{
    run_multi_spot_process_case (recv_mode_, transport_, msg_size_, clients_,
                                 connect_ready_timeout_ms_, "comp_src_spot_server",
                                 "comp_src_spot_client", NULL, duration_seconds_, warmup_seconds_);
}

void run_multi_spot_process_sequence_case (const char *recv_mode_,
                                           const char *transport_,
                                           const char *msg_sizes_,
                                           int clients_,
                                           int connect_ready_timeout_ms_,
                                           int duration_seconds_ = 1,
                                           int warmup_seconds_ = 1)
{
    TEST_ASSERT_NOT_NULL (msg_sizes_);

    process_capture_t *server = create_process_capture ();
    process_capture_t *client = create_process_capture ();
    register_active_process (server);
    register_active_process (client);

    const std::string server_path = sibling_binary_path (g_self_path, "comp_src_spot_server");
    const std::string client_path = sibling_binary_path (g_self_path, "comp_src_spot_client");

    std::vector<std::string> common_env;
    common_env.push_back (std::string ("PERF_MSG_SIZES=") + msg_sizes_);
    {
        char buf[32];
        snprintf (buf, sizeof (buf), "PERF_DURATION_SECONDS=%d", duration_seconds_);
        common_env.push_back (buf);
    }
    {
        char buf[32];
        snprintf (buf, sizeof (buf), "PERF_WARMUP_SECONDS=%d", warmup_seconds_);
        common_env.push_back (buf);
    }
    common_env.push_back ("PERF_SETTLE_MS=500");
    {
        char buf[32];
        snprintf (buf, sizeof (buf), "PERF_CLIENTS=%d", clients_);
        common_env.push_back (buf);
    }
    {
        char buf[64];
        snprintf (buf, sizeof (buf), "PERF_CONNECT_READY_TIMEOUT_MS=%d", connect_ready_timeout_ms_);
        common_env.push_back (buf);
    }
    common_env.push_back ("PERF_DEFAULT_IO_THREADS=4");
    common_env.push_back (std::string ("PERF_RECV_MODE=") + recv_mode_);

    std::vector<std::string> msg_sizes;
    {
        const std::string raw_sizes = msg_sizes_;
        std::string::size_type start = 0;
        while (start < raw_sizes.size ()) {
            const std::string::size_type comma = raw_sizes.find (',', start);
            const std::string token = raw_sizes.substr (
              start, comma == std::string::npos ? std::string::npos : comma - start);
            if (!token.empty ())
                msg_sizes.push_back (token);
            if (comma == std::string::npos)
                break;
            start = comma + 1;
        }
    }
    TEST_ASSERT_FALSE (msg_sizes.empty ());

    std::vector<std::string> server_args;
    server_args.push_back ("zlink");
    server_args.push_back (transport_);
    TEST_ASSERT_TRUE (start_process (server_path, server_args, common_env, server));

    std::string ready_line;
    TEST_ASSERT_TRUE_MESSAGE (wait_for_stdout_prefix (server, "READY,", 10000, &ready_line),
                              capture_case_debug_text (server, client).c_str ());
    std::string endpoint = ready_line.substr (strlen ("READY,"));
    if (!endpoint.empty () && endpoint[endpoint.size () - 1] == '\n')
        endpoint.erase (endpoint.size () - 1);
    std::string control_ready_line;
    TEST_ASSERT_TRUE_MESSAGE (
      wait_for_stdout_prefix (server, "CONTROL_READY,", 10000, &control_ready_line),
      capture_case_debug_text (server, client).c_str ());
    std::string control_endpoint = control_ready_line.substr (strlen ("CONTROL_READY,"));
    if (!control_endpoint.empty () && control_endpoint[control_endpoint.size () - 1] == '\n')
        control_endpoint.erase (control_endpoint.size () - 1);

    std::vector<std::string> client_args;
    client_args.push_back ("zlink");
    client_args.push_back (transport_);
    client_args.push_back (msg_sizes.front ());
    client_args.push_back ("--endpoint");
    client_args.push_back (endpoint);
    client_args.push_back ("--control-endpoint");
    client_args.push_back (control_endpoint);

    TEST_ASSERT_TRUE (start_process (client_path, client_args, common_env, client));

    const int ready_timeout_ms = is_secure_transport_name (transport_) ? 60000 : 30000;

    forward_client_spot_endpoints (server, client, ready_timeout_ms);

    for (size_t i = 0; i < msg_sizes.size (); ++i) {
        const std::string ready_prefix = std::string ("CLIENT_READY,") + msg_sizes[i];
        TEST_ASSERT_TRUE_MESSAGE (
          wait_for_stdout_prefix (client, ready_prefix.c_str (), ready_timeout_ms, NULL),
          capture_case_debug_text (server, client).c_str ());
        write_stdin_line (server, (std::string ("START,") + msg_sizes[i] + "\n").c_str ());
        write_stdin_line (client, (std::string ("START,") + msg_sizes[i] + "\n").c_str ());
    }

    int client_rc = INT_MIN;
    const int exit_timeout_ms = std::max (
      resolve_case_exit_timeout_ms (transport_, msg_sizes_, clients_, duration_seconds_,
                                    warmup_seconds_),
      (duration_seconds_ + warmup_seconds_ + 15) * static_cast<int> (msg_sizes.size ()) * 1000);
    TEST_ASSERT_TRUE_MESSAGE (wait_for_exit_code (client, exit_timeout_ms, &client_rc),
                              capture_case_debug_text (server, client).c_str ());
    close_process_capture (client);
    TEST_ASSERT_EQUAL_INT_MESSAGE (0, client_rc, capture_case_debug_text (server, client).c_str ());

    size_t throughput_count = 0;
    {
        std::lock_guard<std::mutex> lock (client->sync);
        for (size_t i = 0; i < client->stdout_lines.size (); ++i) {
            if (client->stdout_lines[i].find ("throughput") != std::string::npos) {
                ++throughput_count;
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT (msg_sizes.size (), throughput_count);

    destroy_process_capture (client);
    stop_server_process (server, exit_timeout_ms);
    destroy_process_capture (server);
}

void run_multi_spot_invalid_mode_case ()
{
    process_capture_t *client = create_process_capture ();
    register_active_process (client);
    const std::string client_path = sibling_binary_path (g_self_path, "comp_src_spot_client");

    std::vector<std::string> client_args;
    client_args.push_back ("zlink");
    client_args.push_back ("tcp");
    client_args.push_back ("64");
    client_args.push_back ("--endpoint");
    client_args.push_back ("tcp://127.0.0.1:1");

    std::vector<std::string> client_env;
    client_env.push_back ("PERF_RECV_MODE=recv-thread");
    client_env.push_back ("PERF_MULTI_PATTERN=MULTI_SPOT");

    TEST_ASSERT_TRUE (start_process (client_path, client_args, client_env, client));

    int client_rc = INT_MIN;
    TEST_ASSERT_TRUE (wait_for_exit_code (client, 15000, &client_rc));
    close_process_capture (client);
    TEST_ASSERT_NOT_EQUAL (0, client_rc);
    TEST_ASSERT_TRUE (client->stderr_text.find ("policy violation") != std::string::npos);
    destroy_process_capture (client);
}

void setUp ()
{
}

void tearDown ()
{
    cleanup_active_processes ();
}

void test_multi_spot_process_recv_smoke ()
{
    run_multi_spot_process_case ("recv", "tcp", "64", 2, 15000, 2, 2);
}

void test_multi_spot_process_recv_many_clients_tcp_large_smoke ()
{
    run_multi_spot_process_case ("recv", "tcp", "65536", 100, 5000);
}

void test_multi_spot_process_recv_many_clients_tcp_very_large_smoke ()
{
    run_multi_spot_process_case ("recv", "tcp", "131072", 100, 5000, 2, 2);
}

void test_multi_spot_process_recv_many_clients_tcp_large_sequence ()
{
    run_multi_spot_process_sequence_case ("recv", "tcp", "64,256,1024,65536,131072,262144", 100,
                                          5000, 2, 2);
}

void test_multi_spot_process_recv_1000_clients_tcp_ready_count ()
{
    run_multi_spot_process_case ("recv", "tcp", "64", 1000, 20000, 1, 1);
}

void test_multi_spot_process_recv_100_clients_tcp_ready_count ()
{
    run_multi_spot_process_case ("recv", "tcp", "64", 100, 20000, 1, 1);
}

void test_multi_spot_process_recv_200_clients_tcp_ready_count ()
{
    run_multi_spot_process_case ("recv", "tcp", "64", 200, 20000, 1, 1);
}

void test_multi_spot_process_recv_400_clients_tcp_ready_count ()
{
    run_multi_spot_process_case ("recv", "tcp", "64", 400, 20000, 1, 1);
}

void test_multi_spot_process_recv_800_clients_tcp_ready_count ()
{
    run_multi_spot_process_case ("recv", "tcp", "64", 800, 20000, 1, 1);
}

void test_multi_spot_process_recv_many_clients_tls_very_large_smoke ()
{
    run_multi_spot_process_case ("recv", "tls", "262144", 100, 10000, 2, 2);
}

void test_multi_spot_process_recv_many_clients_tls_large_sequence ()
{
    run_multi_spot_process_sequence_case ("recv", "tls", "65536,131072,262144", 100, 10000, 1, 1);
}

void test_multi_spot_process_recv_many_clients_ws_very_large_smoke ()
{
    run_multi_spot_process_case ("recv", "ws", "262144", 100, 10000, 2, 2);
}

void test_multi_spot_process_recv_many_clients_wss_large_sequence ()
{
    run_multi_spot_process_sequence_case ("recv", "wss", "64,256,1024,65536,131072,262144", 100,
                                          10000, 2, 2);
}

void test_multi_spot_process_recv_many_clients_wss_perf_window_sequence ()
{
    run_multi_spot_process_sequence_case ("recv", "wss", "64,256,65536,262144", 100, 10000, 1, 1);
}

void test_multi_spot_process_recv_many_clients_wss_perf_window_tight_ready_timeout ()
{
    run_multi_spot_process_sequence_case ("recv", "wss", "64,256,65536,262144", 100, 5000, 1, 1);
}

void test_multi_spot_reqrep_process_direct_route_smoke ()
{
    std::vector<std::string> env;
    env.push_back ("ZLINK_ENABLE_SPOT_DIRECT_ROUTE=1");
    run_multi_spot_process_case ("recv", "tcp", "64", 1, 15000, "comp_src_spot_reqrep_server",
                                 "comp_src_spot_reqrep_client", &env, 1, 1);
}

void test_multi_spot_sendsend_process_direct_route_smoke ()
{
    std::vector<std::string> env;
    env.push_back ("ZLINK_ENABLE_SPOT_DIRECT_ROUTE=1");
    run_multi_spot_process_case ("recv", "tcp", "64", 1, 15000, "comp_src_spot_sendsend_server",
                                 "comp_src_spot_sendsend_client", &env, 1, 1);
}

void test_multi_spot_process_invalid_mode_is_rejected ()
{
    run_multi_spot_invalid_mode_case ();
}

int main (int argc, char **argv)
{
    signal (SIGPIPE, SIG_IGN);
    g_self_path = argc > 0 ? argv[0] : NULL;
    UNITY_BEGIN ();
#define RUN_SPOT_PROCESS_TEST(name)                                                                \
    do {                                                                                           \
        if (should_run_spot_process_test (#name))                                                  \
            RUN_TEST (name);                                                                       \
    } while (0)
    RUN_SPOT_PROCESS_TEST (test_multi_spot_process_recv_smoke);
    RUN_SPOT_PROCESS_TEST (test_multi_spot_process_recv_many_clients_tcp_large_sequence);
    RUN_SPOT_PROCESS_TEST (test_multi_spot_process_recv_many_clients_tls_large_sequence);
    RUN_SPOT_PROCESS_TEST (test_multi_spot_reqrep_process_direct_route_smoke);
    RUN_SPOT_PROCESS_TEST (test_multi_spot_sendsend_process_direct_route_smoke);
    RUN_SPOT_PROCESS_TEST (test_multi_spot_process_invalid_mode_is_rejected);
#undef RUN_SPOT_PROCESS_TEST
    return UNITY_END ();
}

#else

int main ()
{
    TEST_IGNORE_MESSAGE ("POSIX-only process regression test");
    return 77;
}

#endif
