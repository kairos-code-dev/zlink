/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_CORE_OPERATION_STATE_OWNER_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_CORE_OPERATION_STATE_OWNER_HPP_INCLUDED

#include <memory>
#include <utility>

namespace zlink
{
namespace detail
{

template <typename State>
std::unique_ptr<State> make_operation_state (State &&state_)
{
    return std::make_unique<State> (std::move (state_));
}

template <typename State>
State &operation_state (std::unique_ptr<State> &state_) noexcept
{
    return *state_;
}

template <typename State>
const State &operation_state (const std::unique_ptr<State> &state_) noexcept
{
    return *state_;
}

} // namespace detail
} // namespace zlink

#endif
