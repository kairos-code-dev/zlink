/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/Contracts/messages.hpp"
#include "../Configuration/sample_topology.hpp"

#include <nlohmann/json.hpp>

#include <sys/file.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace zlink::samples::shoppingmall
{

class file_state_store_t
{
  public:
    template <typename TFunc> decltype (auto) update (TFunc &&func) const
    {
        locked_file_t lock{_path + ".lock"};
        auto state = load ();
        if constexpr (std::is_void_v<std::invoke_result_t<TFunc, nlohmann::json &>>) {
            func (state);
            save (state);
            return;
        } else {
            decltype (auto) result = func (state);
            save (state);
            return result;
        }
    }

    template <typename TFunc> decltype (auto) read (TFunc &&func) const
    {
        locked_file_t lock{_path + ".lock"};
        auto state = load ();
        return func (state);
    }

    void seed_defaults () const
    {
        update ([] (nlohmann::json &state) {
            if (!state.empty ()) return;
            state = nlohmann::json::object ();
            state["nextOrderSequence"] = 0;
            state["idempotency"] = nlohmann::json::object ();
            state["orderPaymentMethods"] = nlohmann::json::object ();
            state["events"] = nlohmann::json::object ();
            state["readModels"] = nlohmann::json::object ();
            state["inventory"] = {{"sku-ok", 100}, {"sku-rare", 0}};
            state["releasedReservations"] = nlohmann::json::object ();
            state["payments"] = nlohmann::json::object ();
            state["paymentAttempts"] = nlohmann::json::object ();
        });
    }

  private:
    class locked_file_t
    {
      public:
        explicit locked_file_t (const std::string &path)
        {
            std::filesystem::create_directories (std::filesystem::path (path).parent_path ());
            _fd = ::open (path.c_str (), O_CREAT | O_RDWR, 0666);
            if (_fd < 0) throw std::runtime_error ("failed to open ShoppingMall state lock");
            if (::flock (_fd, LOCK_EX) != 0)
                throw std::runtime_error ("failed to lock ShoppingMall state");
        }

        locked_file_t (const locked_file_t &) = delete;
        locked_file_t &operator= (const locked_file_t &) = delete;

        ~locked_file_t ()
        {
            if (_fd >= 0) {
                ::flock (_fd, LOCK_UN);
                ::close (_fd);
            }
        }

      private:
        int _fd{-1};
    };

    nlohmann::json load () const
    {
        if (!std::filesystem::exists (_path)) return nlohmann::json::object ();
        std::ifstream input (_path);
        if (!input.good () || input.peek () == std::ifstream::traits_type::eof ())
            return nlohmann::json::object ();
        nlohmann::json state;
        input >> state;
        return state;
    }

    void save (const nlohmann::json &state) const
    {
        std::filesystem::create_directories (std::filesystem::path (_path).parent_path ());
        const auto temp = _path + ".tmp";
        {
            std::ofstream output (temp, std::ios::trunc);
            output << state.dump (2);
        }
        std::filesystem::rename (temp, _path);
    }

    std::string _path = shoppingmall_state_file ();
};

inline std::int64_t now_unix_ms ()
{
    return std::chrono::duration_cast<std::chrono::milliseconds> (
             std::chrono::system_clock::now ().time_since_epoch ())
      .count ();
}

inline order_state_t state_from_json (const nlohmann::json &json)
{
    return json.get<order_state_t> ();
}

inline std::vector<std::string> event_types_for (const nlohmann::json &state,
                                                 const std::string &order_id)
{
    std::vector<std::string> result;
    if (!state["events"].contains (order_id)) return result;
    for (const auto &event : state["events"][order_id]) {
        result.push_back (event.value ("type", ""));
    }
    return result;
}

inline bool has_sequence (const nlohmann::json &state,
                          const std::string &order_id,
                          const std::vector<std::string> &expected)
{
    return event_types_for (state, order_id) == expected;
}

inline bool has_prefix (const nlohmann::json &state,
                        const std::string &order_id,
                        const std::vector<std::string> &expected)
{
    const auto actual = event_types_for (state, order_id);
    return actual.size () >= expected.size ()
           && std::equal (expected.begin (), expected.end (), actual.begin ());
}

} // namespace zlink::samples::shoppingmall
