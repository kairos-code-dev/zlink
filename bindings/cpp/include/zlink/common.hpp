/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_COMMON_HPP_INCLUDED
#define ZLINK_CPP_COMMON_HPP_INCLUDED

#include <zlink.h>

#include <chrono>
#include <cstddef>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#if defined(ZLINK_CPP_EXCEPTIONS)
#include <exception>
#include <stdexcept>
#endif

#if __cplusplus >= 201703L
/**
 * @brief Marks return values that should not be ignored.
 */
#define ZLINK_CPP_NODISCARD [[nodiscard]]
#else
#define ZLINK_CPP_NODISCARD
#endif

#if defined(__has_cpp_attribute)
#if __has_cpp_attribute(deprecated)
#define ZLINK_CPP_DEPRECATED(msg_) [[deprecated(msg_)]]
#else
#define ZLINK_CPP_DEPRECATED(msg_)
#endif
#elif defined(_MSC_VER)
#define ZLINK_CPP_DEPRECATED(msg_) __declspec(deprecated(msg_))
#elif defined(__GNUC__) || defined(__clang__)
#define ZLINK_CPP_DEPRECATED(msg_) __attribute__ ((deprecated(msg_)))
#else
#define ZLINK_CPP_DEPRECATED(msg_)
#endif

#endif
