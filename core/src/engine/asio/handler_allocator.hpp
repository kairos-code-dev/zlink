/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_ASIO_HANDLER_ALLOCATOR_HPP_INCLUDED__
#define __ZLINK_ASIO_HANDLER_ALLOCATOR_HPP_INCLUDED__

//  Phase 2 Optimization: Zero-allocation Handlers
//
//  This header provides a custom allocator for ASIO async handlers that
//  avoids heap allocations for small lambda captures. Each poll_entry_t
//  has its own handler_allocator instances, allowing the allocator to
//  recycle memory for the common case of async_wait handlers.
//
//  Expected improvement: 10-15% for high-frequency event patterns
//  like ROUTER multipart messages where callbacks fire rapidly.
//
//  Based on the ASIO "Custom Memory Allocation" example:
//  https://www.boost.org/doc/libs/release/doc/html/boost_asio/example/cpp11/allocation/server.cpp

#include <cstddef>
#include <memory>
#include <type_traits>

namespace zlink
{

//  Recycling allocator for async handlers.
//  Provides a 256-byte inline buffer to avoid heap allocations for
//  typical lambda captures (pointer + a few state flags).
//
//  Thread-safety: Each handler_allocator should be used by a single
//  poll_entry_t's async operations. No synchronization is needed
//  since ASIO poll/run is single-threaded within an io_context.
class handler_allocator
{
  public:
    handler_allocator () : _in_use (false) {}

    //  Non-copyable and non-movable (owns inline storage)
    handler_allocator (const handler_allocator &) = delete;
    handler_allocator &operator= (const handler_allocator &) = delete;

    void *allocate (std::size_t size)
    {
        if (!_in_use && size <= sizeof (_storage)) {
            _in_use = true;
            return &_storage;
        }
        //  Fall back to heap for larger allocations
        return ::operator new (size);
    }

    void deallocate (void *pointer)
    {
        if (pointer == &_storage) {
            _in_use = false;
        } else {
            ::operator delete (pointer);
        }
    }

  private:
    //  Inline storage for small allocations
    //  1024 bytes accommodates typical lambda captures with ASIO wrapper
    //  including the custom_alloc_handler wrapper itself:
    //  - this pointer (8 bytes)
    //  - entry_ pointer (8 bytes)
    //  - ASIO internal wrapper (~80-200 bytes)
    //  - custom_alloc_handler wrapper (~200-400 bytes)
    //  - Additional state/padding
    typename std::aligned_storage<1024>::type _storage;
    bool _in_use;
};


//  STL-compliant allocator that wraps handler_allocator.
//  Required for Boost.Asio 1.74+ which uses the associated_allocator
//  mechanism instead of the deprecated asio_handler_allocate hooks.
template <typename T>
class recycling_allocator
{
  public:
    using value_type = T;

    explicit recycling_allocator (handler_allocator &alloc) noexcept :
        _allocator (&alloc)
    {
    }

    template <typename U>
    recycling_allocator (const recycling_allocator<U> &other) noexcept :
        _allocator (other._allocator)
    {
    }

    T *allocate (std::size_t n)
    {
        return static_cast<T *> (_allocator->allocate (n * sizeof (T)));
    }

    void deallocate (T *p, std::size_t /*n*/) noexcept
    {
        _allocator->deallocate (p);
    }

    template <typename U>
    bool operator== (const recycling_allocator<U> &other) const noexcept
    {
        return _allocator == other._allocator;
    }

    template <typename U>
    bool operator!= (const recycling_allocator<U> &other) const noexcept
    {
        return _allocator != other._allocator;
    }

  private:
    template <typename U>
    friend class recycling_allocator;

    handler_allocator *_allocator;
};


//  Custom handler wrapper that uses handler hooks for custom allocation.
//  This wrapper stores a reference to the handler_allocator and allows
//  ASIO to use it via the associated_allocator mechanism (Boost 1.74+)
//  and the legacy asio_handler_allocate/deallocate hooks (older versions).
template <typename Handler>
class custom_alloc_handler
{
  public:
    custom_alloc_handler (handler_allocator &alloc, Handler h) :
        _allocator (alloc), _handler (std::move (h))
    {
    }

    //  Allow move construction for efficiency
    custom_alloc_handler (custom_alloc_handler &&other) :
        _allocator (other._allocator), _handler (std::move (other._handler))
    {
    }

    //  Copy constructor needed for some ASIO internals
    custom_alloc_handler (const custom_alloc_handler &other) :
        _allocator (other._allocator), _handler (other._handler)
    {
    }

    template <typename... Args>
    void operator() (Args &&...args)
    {
        _handler (std::forward<Args> (args)...);
    }

    //  Legacy ASIO handler hooks (found via ADL, for Boost < 1.74)
    friend void *asio_handler_allocate (std::size_t size,
                                        custom_alloc_handler *this_handler)
    {
        return this_handler->_allocator.allocate (size);
    }

    friend void asio_handler_deallocate (void *pointer,
                                         std::size_t /*size*/,
                                         custom_alloc_handler *this_handler)
    {
        this_handler->_allocator.deallocate (pointer);
    }

    //  Associated allocator support (for Boost.Asio 1.74+)
    using allocator_type = recycling_allocator<void>;
    allocator_type get_allocator () const noexcept
    {
        return allocator_type{_allocator};
    }

  private:
    handler_allocator &_allocator;
    Handler _handler;
};


//  Helper function to create a custom_alloc_handler.
//  Usage:
//    entry_->descriptor.async_wait(
//        wait_read,
//        make_custom_alloc_handler(entry_->in_allocator, [this, entry_](...) { ... }));
template <typename Handler>
inline custom_alloc_handler<Handler>
make_custom_alloc_handler (handler_allocator &alloc, Handler h)
{
    return custom_alloc_handler<Handler> (alloc, std::move (h));
}

} // namespace zlink

#endif // __ZLINK_ASIO_HANDLER_ALLOCATOR_HPP_INCLUDED__
