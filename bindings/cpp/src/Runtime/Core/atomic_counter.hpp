/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_ATOMIC_COUNTER_HPP_INCLUDED
#define ZLINK_CPP_ATOMIC_COUNTER_HPP_INCLUDED

#include <zlink/Contracts/Core/routing_id.hpp>

namespace zlink
{

/**
 * @brief RAII wrapper for a lock-free atomic counter.
 */
class atomic_counter_t
{
  public:
    /**
     * @brief Create a new atomic counter.
     */
    atomic_counter_t () : _counter (zlink_atomic_counter_new ()) {}
    /**
     * @brief Release the underlying counter handle.
     */
    ~atomic_counter_t () { destroy (); }

    atomic_counter_t (atomic_counter_t &&other) noexcept : _counter (other._counter)
    {
        other._counter = NULL;
    }

    atomic_counter_t &operator= (atomic_counter_t &&other) noexcept
    {
        if (this == &other)
            return *this;
        destroy ();
        _counter = other._counter;
        other._counter = NULL;
        return *this;
    }

    atomic_counter_t (const atomic_counter_t &) = delete;
    atomic_counter_t &operator= (const atomic_counter_t &) = delete;

    /**
     * @brief Set the current counter value.
     * @param value_ New counter value.
     */
    void set (int value_) { zlink_atomic_counter_set (_counter, value_); }
    /**
     * @brief Increment the counter.
     * @return Incremented value.
     */
    int inc () { return zlink_atomic_counter_inc (_counter); }
    /**
     * @brief Decrement the counter.
     * @return Decremented value.
     */
    int dec () { return zlink_atomic_counter_dec (_counter); }
    /**
     * @brief Read the current counter value.
     * @return Current value.
     */
    int value () const { return zlink_atomic_counter_value (_counter); }

    /**
     * @brief Explicitly destroy the counter handle.
     */
    void destroy ()
    {
        if (!_counter)
            return;

        void *tmp = _counter;
        _counter = NULL;
        zlink_atomic_counter_destroy (&tmp);
    }

  private:
    void *_counter;
};

} // namespace zlink

#endif
