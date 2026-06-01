/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/errors/error.hpp>

#include <functional>
#include <memory>
#include <typeindex>
#include <type_traits>

namespace zlink::framework
{

namespace detail
{
class service_registry_t;
class service_scope_state_t;
} // namespace detail

enum class service_lifetime_t
{
  singleton,
  scoped,
  transient
};

enum class service_scope_kind_t
{
  handler_invocation,
  stream_session,
  spot_activation,
  entry_spot,
  actor_creation
};

class service_scope_t;

class service_provider_t
{
public:
  service_provider_t ();
  ~service_provider_t ();

  service_provider_t (service_provider_t &&) noexcept;
  service_provider_t &operator= (service_provider_t &&) noexcept;
  service_provider_t (const service_provider_t &) = default;
  service_provider_t &operator= (const service_provider_t &) = default;

  template<typename T>
  T &get_required ()
  {
    return *std::static_pointer_cast<T> (resolve (std::type_index (typeid (T))));
  }

  service_scope_t create_scope (
    service_scope_kind_t kind = service_scope_kind_t::handler_invocation);
  void close () noexcept;
  bool is_closed () const noexcept;

private:
  friend class service_collection_t;
  friend class service_scope_t;

  service_provider_t (std::shared_ptr<detail::service_registry_t> registry,
                      std::shared_ptr<detail::service_scope_state_t> scope);

  std::shared_ptr<void> resolve (std::type_index type);

  std::shared_ptr<detail::service_registry_t> _registry;
  std::shared_ptr<detail::service_scope_state_t> _scope;
};

class service_scope_t
{
public:
  explicit service_scope_t (service_provider_t provider);
  ~service_scope_t ();

  service_scope_t (service_scope_t &&) noexcept;
  service_scope_t &operator= (service_scope_t &&) noexcept;
  service_scope_t (const service_scope_t &) = delete;
  service_scope_t &operator= (const service_scope_t &) = delete;

  template<typename T>
  T &get_required ()
  {
    return _provider.get_required<T> ();
  }

  service_provider_t &provider () noexcept { return _provider; }
  service_scope_kind_t kind () const noexcept;
  void close () noexcept;

private:
  service_provider_t _provider;
};

class service_collection_t
{
public:
  using service_factory_t =
    std::function<std::shared_ptr<void> (service_provider_t &)>;

  service_collection_t ();
  ~service_collection_t ();

  service_collection_t (service_collection_t &&) noexcept;
  service_collection_t &operator= (service_collection_t &&) noexcept;
  service_collection_t (const service_collection_t &) = delete;
  service_collection_t &operator= (const service_collection_t &) = delete;

  template<typename T>
  service_collection_t &add_singleton ()
  {
    static_assert (std::is_default_constructible_v<T>,
                   "add_singleton<T>() requires a default constructor");
    return add_factory<T> ([](service_provider_t &) {
      return std::make_unique<T> ();
    }, service_lifetime_t::singleton);
  }

  template<typename T>
  service_collection_t &add_singleton (std::unique_ptr<T> instance)
  {
    auto shared = std::shared_ptr<T> (std::move (instance));
    return add_descriptor (std::type_index (typeid (T)),
                           service_lifetime_t::singleton,
                           [shared](service_provider_t &) {
                             return shared;
                           });
  }

  template<typename T>
  service_collection_t &add_scoped ()
  {
    static_assert (std::is_default_constructible_v<T>,
                   "add_scoped<T>() requires a default constructor");
    return add_factory<T> ([](service_provider_t &) {
      return std::make_unique<T> ();
    }, service_lifetime_t::scoped);
  }

  template<typename T>
  service_collection_t &add_transient ()
  {
    static_assert (std::is_default_constructible_v<T>,
                   "add_transient<T>() requires a default constructor");
    return add_factory<T> ([](service_provider_t &) {
      return std::make_unique<T> ();
    }, service_lifetime_t::transient);
  }

  template<typename T, typename TFactory>
  service_collection_t &add_factory (
    TFactory factory,
    service_lifetime_t lifetime = service_lifetime_t::transient)
  {
    return add_descriptor (
      std::type_index (typeid (T)),
      lifetime,
      [factory = std::move (factory)](service_provider_t &provider) mutable {
        using factory_result_t =
          decltype (factory (provider));
        if constexpr (std::is_same_v<factory_result_t, std::unique_ptr<T>>) {
          return std::shared_ptr<void> (factory (provider).release ());
        } else if constexpr (std::is_same_v<factory_result_t,
                                            std::shared_ptr<T>>) {
          return std::static_pointer_cast<void> (factory (provider));
        } else {
          return std::shared_ptr<void> (
            std::make_shared<T> (factory (provider)));
        }
      });
  }

  service_provider_t build_provider () const;

private:
  service_collection_t &add_descriptor (std::type_index type,
                                        service_lifetime_t lifetime,
                                        service_factory_t factory);

  std::shared_ptr<detail::service_registry_t> _registry;
};

} // namespace zlink::framework
