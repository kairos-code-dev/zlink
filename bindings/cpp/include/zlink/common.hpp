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

#endif
