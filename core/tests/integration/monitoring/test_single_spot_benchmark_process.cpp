/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#ifndef _WIN32

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

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
        pid (-1), stdout_file (NULL), stderr_file (NULL), stdout_done (false), stderr_done (false)
    {
    }

    pid_t pid;
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

    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    if (pipe (stdout_pipe) != 0 || pipe (stderr_pipe) != 0)
        return false;

    const pid_t pid = fork ();
    if (pid < 0) {
        close (stdout_pipe[0]);
        close (stdout_pipe[1]);
        close (stderr_pipe[0]);
        close (stderr_pipe[1]);
        return false;
    }

    if (pid == 0) {
        dup2 (stdout_pipe[1], STDOUT_FILENO);
        dup2 (stderr_pipe[1], STDERR_FILENO);
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

    close (stdout_pipe[1]);
    close (stderr_pipe[1]);

    out_->pid = pid;
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

bool parse_spot_throughput (const std::vector<std::string> &lines_,
                            const char *transport_,
                            const char *msg_size_,
                            double *throughput_out_)
{
    if (throughput_out_)
        *throughput_out_ = 0.0;
    if (!transport_ || !msg_size_ || !throughput_out_)
        return false;

    const std::string prefix =
      std::string ("RESULT,current,SPOT,") + transport_ + "," + msg_size_ + ",throughput,";
    for (size_t i = 0; i < lines_.size (); ++i) {
        const std::string &line = lines_[i];
        if (line.compare (0, prefix.size (), prefix) != 0)
            continue;

        char *end = NULL;
        const double parsed = std::strtod (line.c_str () + prefix.size (), &end);
        if (end != line.c_str () + prefix.size ()) {
            *throughput_out_ = parsed;
            return true;
        }
    }

    return false;
}

double run_single_spot_process_case (const char *self_path_,
                                     const char *recv_mode_,
                                     const char *transport_,
                                     const char *msg_size_,
                                     int duration_seconds_ = 1,
                                     int warmup_seconds_ = 1,
                                     std::string *debug_text_out_ = NULL)
{
    process_capture_t proc;
    const std::string path = sibling_binary_path (self_path_, "perf_spot");

    std::vector<std::string> args;
    args.push_back ("current");
    args.push_back (transport_);
    args.push_back (msg_size_);

    std::vector<std::string> env;
    env.push_back (std::string ("PERF_RECV_MODE=") + recv_mode_);
    {
        char buf[32];
        snprintf (buf, sizeof (buf), "PERF_SINGLE_DURATION_SECONDS=%d", duration_seconds_);
        env.push_back (buf);
    }
    {
        char buf[32];
        snprintf (buf, sizeof (buf), "PERF_SINGLE_WARMUP_SECONDS=%d", warmup_seconds_);
        env.push_back (buf);
    }

    TEST_ASSERT_TRUE_MESSAGE (start_process (path, args, env, &proc), path.c_str ());

    int rc = INT_MIN;
    const int timeout_ms = std::max (60000, (duration_seconds_ + warmup_seconds_ + 10) * 1000);
    TEST_ASSERT_TRUE_MESSAGE (wait_for_exit_code (&proc, timeout_ms, &rc),
                              capture_debug_text (&proc).c_str ());
    close_process_capture (&proc);
    const std::string debug_text = capture_debug_text (&proc);
    if (debug_text_out_)
        *debug_text_out_ = debug_text;
    TEST_ASSERT_EQUAL_INT_MESSAGE (0, rc, debug_text.c_str ());

    double throughput = 0.0;
    TEST_ASSERT_TRUE_MESSAGE (
      parse_spot_throughput (proc.stdout_lines, transport_, msg_size_, &throughput),
      debug_text.c_str ());
    TEST_ASSERT_TRUE_MESSAGE (throughput > 0.0, debug_text.c_str ());
    return throughput;
}

void run_single_spot_reject_case (const char *self_path_, const char *recv_mode_)
{
    process_capture_t proc;
    const std::string path = sibling_binary_path (self_path_, "perf_spot");

    std::vector<std::string> args;
    args.push_back ("current");
    args.push_back ("tcp");
    args.push_back ("64");

    std::vector<std::string> env;
    env.push_back (std::string ("PERF_RECV_MODE=") + recv_mode_);

    TEST_ASSERT_TRUE (start_process (path, args, env, &proc));

    int rc = INT_MIN;
    TEST_ASSERT_TRUE (wait_for_exit_code (&proc, 15000, &rc));
    close_process_capture (&proc);
    TEST_ASSERT_NOT_EQUAL (0, rc);
    TEST_ASSERT_TRUE (proc.stderr_text.find ("policy violation") != std::string::npos);
}
} // namespace

void test_single_spot_process_recv_smoke ()
{
    (void) run_single_spot_process_case (g_self_path, "recv", "tcp", "64");
}

void test_single_spot_process_callback_is_rejected ()
{
    run_single_spot_reject_case (g_self_path, "callback");
}

void test_single_spot_process_recv_tcp_256_keeps_up_with_64 ()
{
    const double throughput_64 = run_single_spot_process_case (g_self_path, "recv", "tcp", "64");
    const double throughput_256 = run_single_spot_process_case (g_self_path, "recv", "tcp", "256");

    TEST_ASSERT_TRUE (throughput_256 >= throughput_64 * 0.4);
}

void test_single_spot_process_recv_ws_256_smoke ()
{
    (void) run_single_spot_process_case (g_self_path, "recv", "ws", "256", 5, 2);
}

void test_single_spot_process_recv_ws_64_exit_cleanly ()
{
    std::string debug_text;
    const double throughput =
      run_single_spot_process_case (g_self_path, "recv", "ws", "64", 5, 2, &debug_text);
    TEST_ASSERT_TRUE_MESSAGE (throughput > 0.0, debug_text.c_str ());
}

int main (int argc, char **argv)
{
    g_self_path = argc > 0 ? argv[0] : NULL;
    UNITY_BEGIN ();
    RUN_TEST (test_single_spot_process_recv_smoke);
    RUN_TEST (test_single_spot_process_callback_is_rejected);
    return UNITY_END ();
}

#else

int main ()
{
    TEST_IGNORE_MESSAGE ("POSIX-only process regression test");
    return 77;
}

#endif
