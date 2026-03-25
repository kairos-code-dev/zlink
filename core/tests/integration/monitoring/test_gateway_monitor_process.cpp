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

struct gateway_monitor_probe_t
{
    gateway_monitor_probe_t () : send_ready (false), error_code (0) {}

    std::mutex mutex;
    std::condition_variable cv;
    bool send_ready;
    int error_code;
};

gateway_monitor_probe_t *g_gateway_monitor_probe = NULL;

std::string self_path (const char *argv0_)
{
    return argv0_ ? std::string (argv0_) : std::string ();
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

void configure_gateway_handle (void *handle_)
{
    TEST_ASSERT_NOT_NULL (handle_);
    const int zero = 0;
    const int timeout_ms = 200;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (handle_, ZLINK_OPT_LINGER, &zero, sizeof (zero)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (handle_, ZLINK_OPT_SNDTIMEO, &timeout_ms,
                        sizeof (timeout_ms)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (handle_, ZLINK_OPT_RCVTIMEO, &timeout_ms,
                        sizeof (timeout_ms)));
}

void bind_gateway_on_seed (void *gateway_,
                           int *seed_,
                           char *endpoint_,
                           size_t endpoint_size_)
{
    TEST_ASSERT_NOT_NULL (gateway_);
    TEST_ASSERT_NOT_NULL (seed_);
    TEST_ASSERT_NOT_NULL (endpoint_);

    for (int attempt = 0; attempt < 64; ++attempt) {
        std::snprintf (endpoint_, endpoint_size_, "tcp://127.0.0.1:%d",
                       test_port (*seed_));
        if (zlink_gateway_bind (gateway_, endpoint_) == 0) {
            ++(*seed_);
            return;
        }
        ++(*seed_);
    }

    TEST_FAIL_MESSAGE ("gateway bind seed exhausted");
}

void gateway_monitor_handler (const zlink_service_event_t *event_, void *)
{
    gateway_monitor_probe_t *probe = g_gateway_monitor_probe;
    if (!probe || !event_)
        return;

    std::lock_guard<std::mutex> lock (probe->mutex);
    if (event_->event_type == ZLINK_GATEWAY_MONITOR_EVENT_SEND_READY_CHANGED
        && event_->value == 1) {
        probe->send_ready = true;
    } else if (event_->event_type == ZLINK_GATEWAY_MONITOR_EVENT_ERROR) {
        probe->error_code = event_->error_code != 0 ? event_->error_code : EIO;
    }
    probe->cv.notify_all ();
}

bool wait_for_gateway_ready_callback (gateway_monitor_probe_t *probe_,
                                      int timeout_ms_)
{
    if (!probe_)
        return false;

    std::unique_lock<std::mutex> lock (probe_->mutex);
    return probe_->cv.wait_for (
      lock, std::chrono::milliseconds (timeout_ms_), [probe_]() {
          return probe_->send_ready || probe_->error_code != 0;
      })
           && probe_->send_ready && probe_->error_code == 0;
}

int child_server_main ()
{
    void *ctx = zlink_ctx_new ();
    if (!ctx)
        return 2;

    void *server = zlink_gateway_new (ctx);
    if (!server) {
        zlink_ctx_term (ctx);
        return 2;
    }

    configure_gateway_handle (server);
    if (zlink_set_routing_id (server, "gw-server-process",
                              std::strlen ("gw-server-process"))
        != 0) {
        zlink_gateway_destroy (&server);
        zlink_ctx_term (ctx);
        return 2;
    }

    char endpoint[MAX_SOCKET_STRING];
    int bind_seed = 23240;
    bind_gateway_on_seed (server, &bind_seed, endpoint, sizeof (endpoint));
    std::fprintf (stdout, "READY,%s\n", endpoint);
    std::fflush (stdout);

    char line[32];
    while (std::fgets (line, sizeof (line), stdin)) {
        if (std::strcmp (line, "STOP\n") == 0 || std::strcmp (line, "STOP") == 0)
            break;
    }

    zlink_gateway_destroy (&server);
    zlink_ctx_shutdown (ctx);
    zlink_ctx_term (ctx);
    return 0;
}

void test_gateway_ready_with_monitor_callback_across_process ()
{
    process_capture_t server_proc;
    const std::string path = self_path (g_self_path);
    TEST_ASSERT_FALSE (path.empty ());

    std::vector<std::string> args;
    args.push_back ("--child-gateway-server");
    TEST_ASSERT_TRUE (start_process (path, args, &server_proc));

    std::string ready_line;
    TEST_ASSERT_TRUE (
      wait_for_stdout_prefix (&server_proc, "READY,", 10000, &ready_line));
    std::string endpoint = ready_line.substr (std::strlen ("READY,"));
    if (!endpoint.empty () && endpoint[endpoint.size () - 1] == '\n')
        endpoint.erase (endpoint.size () - 1);

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *client = zlink_gateway_new (ctx);
    TEST_ASSERT_NOT_NULL (client);
    configure_gateway_handle (client);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (client, "gw-client-process",
                            std::strlen ("gw-client-process")));

    zlink_service_monitor_open_options_t monitor_opts;
    std::memset (&monitor_opts, 0, sizeof (monitor_opts));
    monitor_opts.events = ZLINK_GATEWAY_MONITOR_EVENT_SEND_READY_CHANGED
                          | ZLINK_GATEWAY_MONITOR_EVENT_ERROR;
    void *monitor = zlink_service_monitor_open (client, &monitor_opts);
    TEST_ASSERT_NOT_NULL (monitor);
    configure_gateway_handle (monitor);

    gateway_monitor_probe_t monitor_probe;
    g_gateway_monitor_probe = &monitor_probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_service_monitor_handler (monitor, &gateway_monitor_handler, NULL));

    zlink_routing_id_t server_rid;
    std::memset (&server_rid, 0, sizeof (server_rid));
    std::memcpy (server_rid.data, "gw-server-process",
                 std::strlen ("gw-server-process"));
    server_rid.size =
      static_cast<uint8_t> (std::strlen ("gw-server-process"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_connect (client, endpoint.c_str (), &server_rid));

    TEST_ASSERT_TRUE (wait_for_gateway_ready_callback (&monitor_probe, 5000));

    g_gateway_monitor_probe = NULL;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_monitor_close (&monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&client));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_shutdown (ctx));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));

    write_stdin_line (&server_proc, "STOP\n");
    int server_rc = INT_MIN;
    TEST_ASSERT_TRUE (wait_for_exit_code (&server_proc, 10000, &server_rc));
    close_process_capture (&server_proc);
    TEST_ASSERT_EQUAL_INT_MESSAGE (0, server_rc, server_proc.stderr_text.c_str ());
}
} // namespace

int main (int argc, char **argv)
{
    signal (SIGPIPE, SIG_IGN);
    g_self_path = argc > 0 ? argv[0] : NULL;

    if (argc > 1 && std::strcmp (argv[1], "--child-gateway-server") == 0)
        return child_server_main ();

    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_gateway_ready_with_monitor_callback_across_process);
    return UNITY_END ();
}

#else

int main ()
{
    TEST_IGNORE_MESSAGE ("POSIX-only process regression test");
    return 77;
}

#endif
