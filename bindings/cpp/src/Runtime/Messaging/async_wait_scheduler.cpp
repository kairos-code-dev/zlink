/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/Contracts/Messaging/operation_contracts.hpp>

#include <thread>

namespace zlink
{
namespace detail
{

void schedule_async_wait (std::function<void ()> task_)
{
    std::thread (std::move (task_)).detach ();
}

} // namespace detail
} // namespace zlink
