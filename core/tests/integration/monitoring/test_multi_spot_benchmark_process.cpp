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

    ~process_capture_t ();

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

process_capture_t::~process_capture_t ()
{
    cleanup_child (this);
}

std::string sibling_binary_path (const char *argv0_, const char *name_)
{
    std::string path = argv0_ ? argv0_ : "";
    const std::string::size_type slash = path.find_last_of ('/');
    if (slash == std::string::npos)
        return std::string (name_);
    return path.substr (0, slash + 1) + name_;
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

void reader_loop (FILE *stream_,
                  process_capture_t *proc_,
                  bool stdout_stream_)
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
    if (pipe (stdin_pipe) != 0 || pipe (stdout_pipe) != 0
        || pipe (stderr_pipe) != 0) {
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
        env_storage.insert (env_storage.end (), env_overrides_.begin (),
                            env_overrides_.end ());

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

    out_->stdout_thread =
      std::thread (&reader_loop, out_->stdout_file, out_, true);
    out_->stderr_thread =
      std::thread (&reader_loop, out_->stderr_file, out_, false);
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
            if (proc_->stdout_lines[i].compare (0, std::strlen (prefix_),
                                                prefix_)
                == 0) {
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

std::string capture_case_debug_text (process_capture_t *server_,
                                     process_capture_t *client_)
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
    TEST_ASSERT_EQUAL_INT (
      static_cast<int> (size),
      static_cast<int> (write (proc_->stdin_fd, line_, size)));
}
} // namespace

void run_multi_spot_process_case (const char *recv_mode_,
                                  const char *transport_,
                                  const char *msg_size_,
                                  int clients_,
                                  int connect_ready_timeout_ms_,
                                  int duration_seconds_ = 1,
                                  int warmup_seconds_ = 1)
{
    process_capture_t server;
    process_capture_t client;

    const std::string server_path =
      sibling_binary_path (g_self_path, "comp_src_spot_server");
    const std::string client_path =
      sibling_binary_path (g_self_path, "comp_src_spot_client");

    std::vector<std::string> common_env;
    common_env.push_back (std::string ("PERF_MSG_SIZES=") + msg_size_);
    {
        char buf[32];
        snprintf (buf, sizeof (buf), "PERF_DURATION_SECONDS=%d",
                  duration_seconds_);
        common_env.push_back (buf);
    }
    {
        char buf[32];
        snprintf (buf, sizeof (buf), "PERF_WARMUP_SECONDS=%d",
                  warmup_seconds_);
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
        snprintf (buf, sizeof (buf), "PERF_CONNECT_READY_TIMEOUT_MS=%d",
                  connect_ready_timeout_ms_);
        common_env.push_back (buf);
    }
    common_env.push_back (std::string ("PERF_RECV_MODE=") + recv_mode_);

    std::vector<std::string> server_args;
    server_args.push_back ("zlink");
    server_args.push_back (transport_);
    TEST_ASSERT_TRUE (
      start_process (server_path, server_args, common_env, &server));

    std::string ready_line;
    TEST_ASSERT_TRUE_MESSAGE (
      wait_for_stdout_prefix (&server, "READY,", 10000, &ready_line),
      capture_case_debug_text (&server, &client).c_str ());
    std::string endpoint = ready_line.substr (strlen ("READY,"));
    if (!endpoint.empty () && endpoint[endpoint.size () - 1] == '\n')
        endpoint.erase (endpoint.size () - 1);

    std::vector<std::string> client_args;
    client_args.push_back ("zlink");
    client_args.push_back (transport_);
    client_args.push_back (msg_size_);
    client_args.push_back ("--endpoint");
    client_args.push_back (endpoint);

    TEST_ASSERT_TRUE (
      start_process (client_path, client_args, common_env, &client));

    const std::string ready_prefix = std::string ("CLIENT_READY,") + msg_size_;
    TEST_ASSERT_TRUE_MESSAGE (
      wait_for_stdout_prefix (&client, ready_prefix.c_str (), 20000, NULL),
      capture_case_debug_text (&server, &client).c_str ());
    write_stdin_line (&server, (std::string ("START,") + msg_size_ + "\n").c_str ());

    int client_rc = INT_MIN;
    const int exit_timeout_ms =
      std::max (30000, (duration_seconds_ + warmup_seconds_ + 10) * 1000);
    TEST_ASSERT_TRUE_MESSAGE (
      wait_for_exit_code (&client, exit_timeout_ms, &client_rc),
                              capture_case_debug_text (&server, &client).c_str ());
    close_process_capture (&client);
    TEST_ASSERT_EQUAL_INT_MESSAGE (0, client_rc,
                                   capture_case_debug_text (&server, &client).c_str ());

    bool saw_throughput = false;
    {
        std::lock_guard<std::mutex> lock (client.sync);
        for (size_t i = 0; i < client.stdout_lines.size (); ++i) {
            if (client.stdout_lines[i].find ("throughput")
                != std::string::npos) {
                saw_throughput = true;
                break;
            }
        }
    }
    TEST_ASSERT_TRUE (saw_throughput);

    cleanup_child (&server);
}

void run_multi_spot_process_sequence_case (
  const char *recv_mode_,
  const char *transport_,
  const char *msg_sizes_,
  int clients_,
  int connect_ready_timeout_ms_,
  int duration_seconds_ = 1,
  int warmup_seconds_ = 1)
{
    TEST_ASSERT_NOT_NULL (msg_sizes_);

    process_capture_t server;
    process_capture_t client;

    const std::string server_path =
      sibling_binary_path (g_self_path, "comp_src_spot_server");
    const std::string client_path =
      sibling_binary_path (g_self_path, "comp_src_spot_client");

    std::vector<std::string> common_env;
    common_env.push_back (std::string ("PERF_MSG_SIZES=") + msg_sizes_);
    {
        char buf[32];
        snprintf (buf, sizeof (buf), "PERF_DURATION_SECONDS=%d",
                  duration_seconds_);
        common_env.push_back (buf);
    }
    {
        char buf[32];
        snprintf (buf, sizeof (buf), "PERF_WARMUP_SECONDS=%d",
                  warmup_seconds_);
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
        snprintf (buf, sizeof (buf), "PERF_CONNECT_READY_TIMEOUT_MS=%d",
                  connect_ready_timeout_ms_);
        common_env.push_back (buf);
    }
    common_env.push_back (std::string ("PERF_RECV_MODE=") + recv_mode_);

    std::vector<std::string> msg_sizes;
    {
        const std::string raw_sizes = msg_sizes_;
        std::string::size_type start = 0;
        while (start < raw_sizes.size ()) {
            const std::string::size_type comma = raw_sizes.find (',', start);
            const std::string token =
              raw_sizes.substr (start, comma == std::string::npos
                                         ? std::string::npos
                                         : comma - start);
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
    TEST_ASSERT_TRUE (
      start_process (server_path, server_args, common_env, &server));

    std::string ready_line;
    TEST_ASSERT_TRUE_MESSAGE (
      wait_for_stdout_prefix (&server, "READY,", 10000, &ready_line),
      capture_case_debug_text (&server, &client).c_str ());
    std::string endpoint = ready_line.substr (strlen ("READY,"));
    if (!endpoint.empty () && endpoint[endpoint.size () - 1] == '\n')
        endpoint.erase (endpoint.size () - 1);

    std::vector<std::string> client_args;
    client_args.push_back ("zlink");
    client_args.push_back (transport_);
    client_args.push_back (msg_sizes.front ());
    client_args.push_back ("--endpoint");
    client_args.push_back (endpoint);

    TEST_ASSERT_TRUE (
      start_process (client_path, client_args, common_env, &client));

    for (size_t i = 0; i < msg_sizes.size (); ++i) {
        const std::string ready_prefix =
          std::string ("CLIENT_READY,") + msg_sizes[i];
        TEST_ASSERT_TRUE_MESSAGE (
          wait_for_stdout_prefix (&client, ready_prefix.c_str (), 30000, NULL),
          capture_case_debug_text (&server, &client).c_str ());
        write_stdin_line (&server,
                          (std::string ("START,") + msg_sizes[i] + "\n")
                            .c_str ());
    }

    int client_rc = INT_MIN;
    const int exit_timeout_ms =
      std::max (60000, (duration_seconds_ + warmup_seconds_ + 15)
                           * static_cast<int> (msg_sizes.size ()) * 1000);
    TEST_ASSERT_TRUE_MESSAGE (
      wait_for_exit_code (&client, exit_timeout_ms, &client_rc),
      capture_case_debug_text (&server, &client).c_str ());
    close_process_capture (&client);
    TEST_ASSERT_EQUAL_INT_MESSAGE (0, client_rc,
                                   capture_case_debug_text (&server, &client).c_str ());

    size_t throughput_count = 0;
    {
        std::lock_guard<std::mutex> lock (client.sync);
        for (size_t i = 0; i < client.stdout_lines.size (); ++i) {
            if (client.stdout_lines[i].find ("throughput")
                != std::string::npos) {
                ++throughput_count;
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT (msg_sizes.size (), throughput_count);

    cleanup_child (&server);
}

void run_multi_spot_invalid_mode_case ()
{
    process_capture_t client;
    const std::string client_path =
      sibling_binary_path (g_self_path, "comp_src_spot_client");

    std::vector<std::string> client_args;
    client_args.push_back ("zlink");
    client_args.push_back ("tcp");
    client_args.push_back ("64");
    client_args.push_back ("--endpoint");
    client_args.push_back ("tcp://127.0.0.1:1");

    std::vector<std::string> client_env;
    client_env.push_back ("PERF_RECV_MODE=recv-thread");
    client_env.push_back ("PERF_MULTI_PATTERN=MULTI_SPOT");

    TEST_ASSERT_TRUE (
      start_process (client_path, client_args, client_env, &client));

    int client_rc = INT_MIN;
    TEST_ASSERT_TRUE (wait_for_exit_code (&client, 15000, &client_rc));
    close_process_capture (&client);
    TEST_ASSERT_NOT_EQUAL (0, client_rc);
    TEST_ASSERT_TRUE (
      client.stderr_text.find ("invalid --recv mode") != std::string::npos);
}

void test_multi_spot_process_recv_smoke ()
{
    run_multi_spot_process_case ("recv", "tcp", "64", 2, 15000);
}

void test_multi_spot_process_callback_smoke ()
{
    run_multi_spot_process_case ("callback", "tcp", "64", 2, 15000, 2, 2);
}

void test_multi_spot_process_recv_many_clients_tcp_large_smoke ()
{
    run_multi_spot_process_case ("recv", "tcp", "65536", 100, 5000);
}

void test_multi_spot_process_recv_many_clients_ws_very_large_smoke ()
{
    run_multi_spot_process_case ("recv", "ws", "262144", 100, 10000, 2, 2);
}

void test_multi_spot_process_recv_many_clients_wss_large_sequence ()
{
    run_multi_spot_process_sequence_case (
      "recv", "wss", "64,256,1024,65536,131072,262144", 100, 10000, 2, 2);
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
    RUN_TEST (test_multi_spot_process_recv_smoke);
    RUN_TEST (test_multi_spot_process_callback_smoke);
    RUN_TEST (test_multi_spot_process_recv_many_clients_tcp_large_smoke);
    RUN_TEST (test_multi_spot_process_recv_many_clients_ws_very_large_smoke);
    RUN_TEST (test_multi_spot_process_recv_many_clients_wss_large_sequence);
    RUN_TEST (test_multi_spot_process_invalid_mode_is_rejected);
    return UNITY_END ();
}

#else

int main ()
{
    TEST_IGNORE_MESSAGE ("POSIX-only process regression test");
    return 77;
}

#endif
