/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink.hpp>

#include <optional>
#include <type_traits>

namespace
{

template <typename T> class has_actor_ref_public_contract_t
{
  private:
    template <typename U>
    static auto test (int) -> decltype (std::declval<const U &> ().node_rid (),
                                        std::declval<const U &> ().actor_id (),
                                        std::declval<const U &> ().generation (),
                                        std::declval<const U &> ().unchecked (),
                                        std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

static_assert (has_actor_ref_public_contract_t<zlink::actor_ref_t>::value,
               "actor.hpp must expose actor_ref_t public API");
static_assert (std::is_class<zlink::service::actor_t>::value,
               "actor.hpp must expose service::actor_t");
static_assert (std::is_same<decltype (zlink::actor_part_t::has_more), bool>::value,
               "actor.hpp must expose actor_part_t");
static_assert (std::is_same<decltype (zlink::actor_route_t::current_spot_rid),
                            std::optional<zlink::routing_id_t>>::value,
               "actor.hpp must expose actor_route_t with optional current_spot_rid");
static_assert (
  std::is_same<decltype (zlink::actor_route_t::current_spot_kind), zlink::spot_kind>::value,
  "actor.hpp must expose actor_route_t current_spot_kind");
static_assert (std::is_same<decltype (std::declval<zlink::stream_socket_t &> ().bind_actor (
                              std::declval<const zlink::routing_id_t &> (),
                              std::declval<const zlink::actor_ref_t &> ())),
                            zlink::service::actor_bind_operation_t>::value,
               "stream_socket_t must expose node-less actor bind builder");
static_assert (std::is_same<decltype (std::declval<zlink::stream_socket_t &> ().send_bound_actor (
                              std::declval<const zlink::routing_id_t &> (),
                              std::declval<const std::string &> ())),
                            zlink::service::send_operation_t>::value,
               "stream_socket_t must expose bound actor send builder");
static_assert (std::is_same<decltype (std::declval<zlink::service::spot_node_t &> ().send_to_actor (
                              std::declval<const zlink::actor_ref_t &> ())),
                            zlink::service::send_operation_t>::value,
               "spot_node_t must expose resolved Actor send builder");
static_assert (
  std::is_same<decltype (std::declval<zlink::service::spot_node_t &> ()
                          .send_to_actor (std::declval<const zlink::actor_ref_t &> ())
                          .message (std::declval<zlink::message_t &> ())
                          .message (std::declval<zlink::message_t &> ())),
               zlink::service::send_submit_operation_t &&>::value,
  "spot_node_t Actor send builder must accept multipart payloads");
static_assert (
  std::is_same<decltype (std::declval<zlink::service::spot_node_t &> ().request_to_actor (
                 std::declval<const zlink::actor_ref_t &> ())),
               zlink::service::request_operation_t>::value,
  "spot_node_t must expose resolved Actor request builder");
static_assert (
  std::is_same<decltype (std::declval<zlink::service::spot_node_t &> ().reply_actor_no_bind (
                 std::declval<const zlink::actor_recv_info_t &> (),
                 std::declval<std::vector<zlink::message_t> &> (),
                 zlink::request_result_t::ok)),
               void>::value,
  "spot_node_t must expose no-bind Actor reply submit");
static_assert (std::is_same<decltype (zlink::actor_recv_info_t::request_id), uint64_t>::value,
               "actor_recv_info_t must expose request_id");
static_assert (std::is_same<decltype (std::declval<zlink::service::actor_t &> ().join (
                              std::declval<zlink::service::spot_t &> ())),
                            zlink::service::actor_join_operation_t>::value,
               "actor_t must expose join builder");
static_assert (std::is_same<decltype (std::declval<zlink::service::spot_node_t &> ()
                                      .join_actor_entry_spot (
                                        std::declval<const zlink::actor_ref_t &> (),
                                        std::declval<const zlink::routing_id_t &> (),
                                        std::declval<zlink::message_t &> ())),
                            zlink::service::actor_join_entry_spot_operation_t>::value,
               "spot_node_t must expose request-bearing Entry Spot join builder");
static_assert (
  !std::is_invocable<decltype (&zlink::service::spot_node_t::join_actor_entry_spot),
                     zlink::service::spot_node_t &, const zlink::actor_ref_t &,
                     const zlink::routing_id_t &>::value,
  "spot_node_t must not expose payload-less Entry Spot join");

} // namespace

int main ()
{
    return 0;
}
