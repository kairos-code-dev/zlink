/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/http_client_runtime.hpp"

#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>

namespace zlink::http_client::detail
{
namespace
{
namespace asio = boost::asio;

class default_coroutine_scheduler_t final
    : public zlink::http_client::coroutine_execute_scheduler_t,
      public zlink::http_client::coroutine_resume_scheduler_t
{
  public:
    default_coroutine_scheduler_t () : _pool (1) {}

    void execute (std::function<void ()> work) override
    {
        asio::post (_pool, [work = std::move (work)] () mutable { work (); });
    }

    void resume (std::function<void ()> continuation) override
    {
        asio::post (_pool, [continuation = std::move (continuation)] () mutable {
            try {
                continuation ();
            }
            catch (...) {
            }
        });
    }

  private:
    asio::thread_pool _pool;
};

std::shared_ptr<default_coroutine_scheduler_t> default_scheduler_instance ()
{
    static auto *scheduler = new std::shared_ptr<default_coroutine_scheduler_t> (
      std::make_shared<default_coroutine_scheduler_t> ());
    return *scheduler;
}

} // namespace

std::shared_ptr<coroutine_execute_scheduler_t> default_coroutine_execute_scheduler ()
{
    return default_scheduler_instance ();
}

std::shared_ptr<coroutine_resume_scheduler_t> default_coroutine_resume_scheduler ()
{
    return default_scheduler_instance ();
}

} // namespace zlink::http_client::detail
