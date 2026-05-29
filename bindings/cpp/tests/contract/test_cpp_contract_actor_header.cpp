/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink.hpp>

#include <optional>
#include <type_traits>

namespace {

template<typename T> class has_actor_ref_public_contract_t
{
  private:
    template<typename U>
    static auto test (int)
      -> decltype (std::declval<const U &> ().node_rid (),
                    std::declval<const U &> ().actor_id (),
                    std::declval<const U &> ().generation (),
                    std::declval<const U &> ().unchecked (),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

static_assert (has_actor_ref_public_contract_t<zlink::actor_ref_t>::value,
               "actor.hpp must expose actor_ref_t public API");
static_assert (std::is_class<zlink::service::actor_t>::value,
               "actor.hpp must expose service::actor_t");
static_assert (std::is_same<decltype (zlink::actor_part_t::has_more), bool>::value,
               "actor.hpp must expose actor_part_t");
static_assert (
  std::is_same<decltype (zlink::actor_route_t::current_spot_rid),
               std::optional<zlink::routing_id_t>>::value,
  "actor.hpp must expose actor_route_t with optional current_spot_rid");
static_assert (
  std::is_same<decltype (zlink::actor_route_t::current_spot_kind),
               zlink::spot_kind>::value,
  "actor.hpp must expose actor_route_t current_spot_kind");
static_assert (
  std::is_same<
    decltype (std::declval<zlink::stream_socket_t &> ()
                .attach_actor_gateway (
                  std::declval<zlink::service::spot_node_t &> ())),
    void>::value,
  "stream_socket_t must expose actor gateway attach");
static_assert (
  std::is_same<
    decltype (std::declval<zlink::stream_socket_t &> ().bind_actor (
      std::declval<const zlink::routing_id_t &> (),
      std::declval<const zlink::actor_ref_t &> ())),
    zlink::service::actor_bind_operation_t>::value,
  "stream_socket_t must expose node-less actor bind builder");
static_assert (
  std::is_same<
    decltype (std::declval<zlink::stream_socket_t &> ().send_bound_actor (
      std::declval<const zlink::routing_id_t &> (),
      std::declval<const std::string &> ())),
    zlink::service::send_operation_t>::value,
  "stream_socket_t must expose bound actor send builder");
static_assert (
  std::is_same<
    decltype (std::declval<zlink::service::actor_t &> ().join (
      std::declval<zlink::service::spot_t &> ())),
    zlink::service::actor_join_operation_t>::value,
  "actor_t must expose join builder");

} // namespace

int main () { return 0; }
