/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Sockets/stream_socket.hpp>
#include <zlink/framework.hpp>

#include <atomic>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace zlink::samples::tictactoe
{

inline bool keep_running_requested () noexcept
{
    return std::getenv ("ZLINK_CPP_SAMPLE_KEEP_RUNNING") != nullptr;
}

class stop_after_start_service_t final : public zlink::framework::hosted_service_t
{
  public:
    explicit stop_after_start_service_t (zlink::framework::app_t &app) : _app (app) {}

    void start (zlink::framework::service_provider_t &) override
    {
        started = true;
        _app.stop ();
    }

    void stop () noexcept override { stopped = true; }

    bool started = false;
    bool stopped = false;

  private:
    zlink::framework::app_t &_app;
};

class sample_stream_server_service_t final : public zlink::framework::hosted_service_t
{
  public:
    using run_server_fn_t = void (*) (zlink::stream_socket_t &,
                                      const std::string &,
                                      const std::string &);

    sample_stream_server_service_t (zlink::framework::app_t &app,
                                    std::string endpoint,
                                    std::string actor_id,
                                    run_server_fn_t run_server) :
        _app (app),
        _endpoint (std::move (endpoint)),
        _actor_id (std::move (actor_id)),
        _run_server (run_server)
    {
    }

    void start (zlink::framework::service_provider_t &) override
    {
        _worker = std::thread ([this] {
            zlink::context_t context;
            zlink::stream_socket_t server (context);
            _server = &server;
            server.options ().notify (false);
            server.bind (_endpoint);
            try {
                _run_server (server, _endpoint, _actor_id);
            }
            catch (...) {
            }
            _server = nullptr;
            _app.stop ();
        });
    }

    void stop () noexcept override
    {
        if (auto *server = _server.load ()) {
            try {
                server->close ();
            }
            catch (...) {
            }
        }
        if (_worker.joinable ()) {
            _worker.join ();
        }
    }

  private:
    zlink::framework::app_t &_app;
    std::string _endpoint;
    std::string _actor_id;
    run_server_fn_t _run_server;
    std::atomic<zlink::stream_socket_t *> _server = nullptr;
    std::thread _worker;
};

} // namespace zlink::samples::tictactoe
