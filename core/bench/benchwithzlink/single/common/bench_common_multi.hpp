#ifndef BENCH_COMMON_MULTI_HPP
#define BENCH_COMMON_MULTI_HPP

#include <chrono>
#include <vector>
#include <string>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <mutex>

struct multi_bench_settings_t
{
    size_t clients;
    int inflight;
    int connect_concurrency;
    int warmup_seconds;
    int measure_seconds;
    int drain_ms;
};

inline int resolve_multi_int_env(const char *env_name, int default_value, int min_value)
{
    if (!env_name)
        return default_value;

    const char *value = std::getenv(env_name);
    if (!value || !*value)
        return default_value;

    char *end = NULL;
    errno = 0;
    const long parsed = std::strtol(value, &end, 10);
    if (errno != 0 || end == value)
        return default_value;

    if (parsed < min_value)
        return min_value;
    return static_cast<int>(parsed);
}

inline multi_bench_settings_t resolve_multi_bench_settings()
{
    multi_bench_settings_t settings;
    settings.clients =
      static_cast<size_t>(resolve_multi_int_env("BENCH_MULTI_CLIENTS", 100, 1));
    settings.inflight = resolve_multi_int_env("BENCH_MULTI_INFLIGHT", 30, 1);
    settings.connect_concurrency =
      resolve_multi_int_env("BENCH_MULTI_CONNECT_CONCURRENCY", 128, 1);
    settings.warmup_seconds = resolve_multi_int_env("BENCH_MULTI_WARMUP_SECONDS", 3, 0);
    settings.measure_seconds = resolve_multi_int_env("BENCH_MULTI_MEASURE_SECONDS", 10, 1);
    settings.drain_ms = resolve_multi_int_env("BENCH_MULTI_DRAIN_MS", 300, 0);
    return settings;
}

class simple_counter_semaphore_t
{
public:
    explicit simple_counter_semaphore_t(int max_inflight)
      : _max_count(max_inflight > 0 ? max_inflight : 1), _count(0)
    {
    }

    void acquire()
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _cv.wait(lock, [this]() { return _count < _max_count; });
        ++_count;
    }

    void release()
    {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            --_count;
        }
        _cv.notify_one();
    }

private:
    const int _max_count;
    int _count;
    std::mutex _mutex;
    std::condition_variable _cv;
};

template <typename ConnectFn>
inline bool connect_clients_concurrently(const std::vector<void *> &sockets,
                                       const std::string &endpoint,
                                       ConnectFn connect_fn,
                                       int max_concurrency)
{
    if (sockets.empty())
        return true;

    std::atomic<bool> ok(true);
    const size_t total = sockets.size();
    size_t start = 0;
    const size_t chunk = max_concurrency > 0 ? static_cast<size_t>(max_concurrency) : 1;

    while (start < total) {
        const size_t end = start + chunk < total ? start + chunk : total;
        std::vector<std::thread> workers;
        for (size_t i = start; i < end; ++i) {
            workers.emplace_back([&, i]() {
                if (!connect_fn(sockets[i], endpoint))
                    ok.store(false, std::memory_order_relaxed);
            });
        }
        for (size_t i = 0; i < workers.size(); ++i)
            workers[i].join();

        if (!ok.load(std::memory_order_relaxed))
            return false;

        start = end;
    }

    return true;
}

template <typename SendFn, typename RecvFn>
inline int run_multi_timed_benchmark(const std::vector<void *> &clients,
                                    const multi_bench_settings_t &settings,
                                    SendFn send_fn,
                                    RecvFn recv_fn,
                                    int seconds,
                                    std::atomic<int> *sent_count)
{
    if (clients.empty() || seconds <= 0)
        return 0;

    const auto start = std::chrono::steady_clock::now();
    const auto measure_end =
      start + std::chrono::seconds(std::max(1, seconds));

    std::atomic<bool> receiver_should_drain(false);
    std::atomic<int> received_count(0);
    std::thread receiver([&]() {
        const int yield_sleep_us = 50;

        while (true) {
            if (recv_fn())
                ++received_count;
            else
                std::this_thread::sleep_for(std::chrono::microseconds(yield_sleep_us));

            const auto now = std::chrono::steady_clock::now();
            if (receiver_should_drain.load(std::memory_order_acquire)
                && now >= measure_end)
                break;
        }

        const auto drain_end =
          std::chrono::steady_clock::now()
          + std::chrono::milliseconds(std::max(0, settings.drain_ms));
        while (std::chrono::steady_clock::now() < drain_end) {
            if (recv_fn())
                ++received_count;
            else
                std::this_thread::sleep_for(std::chrono::microseconds(yield_sleep_us));
        }
    });

    simple_counter_semaphore_t limiter(settings.inflight);
    std::vector<std::thread> senders;
    senders.reserve(clients.size());
    for (size_t i = 0; i < clients.size(); ++i) {
        senders.emplace_back([&, i]() {
            while (std::chrono::steady_clock::now() < measure_end) {
                limiter.acquire();
                const bool ok = send_fn(i);
                limiter.release();
                if (!ok)
                    break;
                if (sent_count)
                    sent_count->fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (size_t i = 0; i < senders.size(); ++i)
        senders[i].join();

    receiver_should_drain.store(true, std::memory_order_release);
    if (receiver.joinable())
        receiver.join();

    return received_count.load(std::memory_order_relaxed);
}

#endif
