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

template <typename T>
concept has_unfenced_bound_session_send_t = requires (
  T &value, const zlink::actor_ref_t &actor,
  const std::vector<zlink::message_t> &parts) {
    value.send_bound_session (actor, parts);
};

template <typename T>
concept has_unfenced_actor_bound_session_send_t = requires (
  T &value, const std::vector<zlink::message_t> &parts) {
    value.send_bound_session (parts);
};

static_assert (has_actor_ref_public_contract_t<zlink::actor_ref_t>::value,
               "actor.hpp must expose actor_ref_t public API");
static_assert (std::is_class<zlink::service::actor_t>::value,
               "actor.hpp must expose service::actor_t");
static_assert (
  std::is_same<decltype (std::declval<zlink::service::mesh_node_t &> ().send_to_actor (
                 std::declval<const zlink::actor_ref_t &> (),
                 std::declval<const std::vector<zlink::message_t> &> ())),
               zlink::submit_result_t>::value,
  "mesh_node_t must expose borrowed multipart Actor send");
static_assert (
  std::is_same<decltype (std::declval<zlink::service::mesh_node_t &> ().request_to_actor (
                 std::declval<const zlink::actor_ref_t &> (),
                 std::declval<const std::vector<zlink::message_t> &> (),
                 std::declval<zlink::service::operation_id_t &> ())),
               zlink::submit_result_t>::value,
  "mesh_node_t must expose correlated Actor request submission");
static_assert (
  std::is_same<decltype (std::declval<zlink::service::mesh_node_t &> ().send_bound_session (
                 std::declval<const zlink::actor_ref_t &> (), std::uint64_t{},
                 std::declval<const std::vector<zlink::message_t> &> ())),
               zlink::submit_result_t>::value,
  "mesh_node_t bound-session send must require the expected binding generation");
static_assert (!has_unfenced_bound_session_send_t<zlink::service::mesh_node_t>,
               "mesh_node_t must not select the current bound-session generation implicitly");
static_assert (
  std::is_same<decltype (std::declval<zlink::service::actor_t &> ().join_spot (
                 std::declval<const zlink::routing_id_t &> (),
                 std::declval<const zlink::routing_id_t &> (), std::uint64_t{},
                 std::declval<const std::vector<zlink::message_t> &> (),
                 std::declval<zlink::service::operation_id_t &> ())),
               zlink::submit_result_t>::value,
  "actor_t must expose correlated Spot join submission");
static_assert (
  std::is_same<decltype (std::declval<zlink::service::actor_t &> ().destroy (
                 std::declval<zlink::service::operation_id_t &> ())),
               zlink::submit_result_t>::value,
  "actor_t must expose correlated destroy submission");
static_assert (
  std::is_same<decltype (std::declval<zlink::service::actor_t &> ().send_bound_session (
                 std::uint64_t{},
                 std::declval<const std::vector<zlink::message_t> &> ())),
               zlink::submit_result_t>::value,
  "actor_t bound-session send must require the expected binding generation");
static_assert (!has_unfenced_actor_bound_session_send_t<zlink::service::actor_t>,
               "actor_t must not select the current bound-session generation implicitly");
static_assert (
  std::is_same<decltype (zlink::service::receive_record_t::source_binding_generation),
               std::uint64_t>::value,
  "receive_record_t must preserve Core's validated source binding generation");
static_assert (
  std::is_same<decltype (zlink::service::actor_join_reply (
                 std::declval<const zlink::service::reply_token_t &> (),
                 zlink::service::actor_join_result_t::accepted,
                 std::declval<const std::vector<zlink::message_t> &> ())),
               zlink::submit_result_t>::value,
  "actor.hpp must expose Actor join reply submission");

} // namespace

int main ()
{
    return 0;
}
