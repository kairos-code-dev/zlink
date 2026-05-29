/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_SERVICE_REQUEST_SUBMITTER_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_SERVICE_REQUEST_SUBMITTER_HPP_INCLUDED

#include <Runtime/Service/detail.hpp>

namespace zlink
{
namespace service
{
namespace detail
{

template <typename SubmitPart>
async_result_t<std::vector<message_t> >
submit_request_parts_async (std::vector<message_t> &parts_,
                            std::function<void ()> progress_,
                            SubmitPart submit_part_)
{
    std::unique_ptr<request_state_t> state (make_future_request_state ());
    std::future<std::vector<message_t> > future = state->promise->get_future ();
    std::vector<zlink_msg_t> native;
    if (move_parts_to_native (parts_, native) != 0)
        throw last_error ();

    size_t failed_index = 0;
    const submit_result_t rc =
      static_cast<submit_result_t> (submit_native_parts (
        native, failed_index,
        [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_,
             bool is_final_) {
            return submit_part_ (part_out_, part_flag_,
                                 is_final_ ? &request_callback_trampoline
                                           : nullptr,
                                 is_final_ ? state.get () : nullptr);
        }));
    if (rc != submit_result_t::ok) {
        close_native_parts (native, failed_index);
        throw submit_error_t (rc, zlink_errno ());
    }

    state.release ();
    return async_result_t<std::vector<message_t> > (std::move (future),
                                                    std::move (progress_));
}

template <typename SubmitPart>
bool submit_request_parts_callback (std::vector<message_t> &parts_,
                                    request_callback_t callback_,
                                    send_flags_t flags_,
                                    SubmitPart submit_part_)
{
    std::unique_ptr<request_state_t> state (
      make_callback_request_state (std::move (callback_)));
    std::vector<zlink_msg_t> native;
    if (move_parts_to_native (parts_, native) != 0)
        throw last_error ();

    size_t failed_index = 0;
    const submit_result_t rc =
      static_cast<submit_result_t> (submit_native_parts (
        native, failed_index,
        [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_,
             bool is_final_) {
            return submit_part_ (part_out_, part_flag_,
                                 is_final_ ? &request_callback_trampoline
                                           : nullptr,
                                 is_final_ ? state.get () : nullptr);
        }));
    if (rc != submit_result_t::ok) {
        close_native_parts (native, failed_index);
        if (flags_ == send_flags_t::dontwait
            && rc == submit_result_t::backpressured)
            return false;
        throw submit_error_t (rc, zlink_errno ());
    }

    state.release ();
    return true;
}

} // namespace detail
} // namespace service
} // namespace zlink

#endif
