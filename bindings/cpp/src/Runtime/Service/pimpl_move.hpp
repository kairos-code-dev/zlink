/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_SERVICE_PIMPL_MOVE_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_SERVICE_PIMPL_MOVE_HPP_INCLUDED

#include <memory>
#include <utility>

namespace zlink::service::detail
{

template <typename ImplT>
inline void move_construct_pimpl (std::unique_ptr<ImplT> &target_impl_,
                                  int &target_last_error_,
                                  std::unique_ptr<ImplT> &source_impl_,
                                  int &source_last_error_) noexcept
{
    target_impl_ = std::move (source_impl_);
    target_last_error_ = source_last_error_;
    if (!source_impl_)
        source_impl_ = std::make_unique<ImplT> ();
    source_last_error_ = 0;
}

template <typename OwnerT, typename ImplT>
inline OwnerT &move_assign_pimpl_and_close (OwnerT &target_,
                                            OwnerT &source_,
                                            std::unique_ptr<ImplT> &target_impl_,
                                            int &target_last_error_,
                                            std::unique_ptr<ImplT> &source_impl_,
                                            int &source_last_error_) noexcept
{
    if (&target_ == &source_)
        return target_;
    try {
        target_.close ();
    }
    catch (...) {
    }
    move_construct_pimpl (target_impl_, target_last_error_, source_impl_, source_last_error_);
    return target_;
}

} // namespace zlink::service::detail

#endif
