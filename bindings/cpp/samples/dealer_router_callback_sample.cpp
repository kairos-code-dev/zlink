/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.hpp"

namespace {

struct router_callback_result_t
{
    zlink::routing_id_t routing_id;
    std::string payload;
};

struct dealer_callback_result_t
{
    std::string payload;
};

void router_callback (const zlink_routing_id_t *source_rid_,
                      zlink_msg_t *parts_,
                      size_t part_count_,
                      void *userdata_)
{
    std::promise<router_callback_result_t> *result_promise =
      static_cast<std::promise<router_callback_result_t> *> (userdata_);
    assert (result_promise != NULL);
    assert (source_rid_ != NULL);
    assert (part_count_ == 1);

    router_callback_result_t result;
    result.routing_id = zlink::routing_id_t (source_rid_->data, source_rid_->size);
    result.payload.assign (
      static_cast<const char *> (zlink_msg_data (&parts_[0])),
      zlink_msg_size (&parts_[0]));

    zlink_multipart_close (parts_, part_count_);
    result_promise->set_value (result);
}

void dealer_callback (const zlink_routing_id_t *,
                      zlink_msg_t *parts_,
                      size_t part_count_,
                      void *userdata_)
{
    std::promise<dealer_callback_result_t> *result_promise =
      static_cast<std::promise<dealer_callback_result_t> *> (userdata_);
    assert (result_promise != NULL);
    assert (part_count_ == 1);

    dealer_callback_result_t result;
    result.payload.assign (
      static_cast<const char *> (zlink_msg_data (&parts_[0])),
      zlink_msg_size (&parts_[0]));

    zlink_multipart_close (parts_, part_count_);
    result_promise->set_value (result);
}

} // namespace

int main ()
{
    zlink::context_t ctx;
    zlink::router_socket_t router (ctx);
    zlink::dealer_socket_t dealer (ctx);
    zlink::monitor_handle_t router_monitor = router.monitor_handle ();
    zlink::monitor_handle_t dealer_monitor = dealer.monitor_handle ();

    assert (router.bind ("tcp://127.0.0.1:0") == 0);
    std::string endpoint;
    assert (router.get_option (zlink::socket_options::last_endpoint, endpoint)
            == 0);
    assert (!endpoint.empty ());
    assert (dealer.connect (endpoint) == 0);
    assert (detail::wait_connected (router_monitor, dealer_monitor));

    std::promise<router_callback_result_t> router_result_promise;
    std::future<router_callback_result_t> router_result_future =
      router_result_promise.get_future ();
    std::promise<dealer_callback_result_t> dealer_result_promise;
    std::future<dealer_callback_result_t> dealer_result_future =
      dealer_result_promise.get_future ();
    assert (router.on_receive (&router_callback, &router_result_promise) == 0);
    assert (dealer.on_receive (&dealer_callback, &dealer_result_promise) == 0);

    const std::string sent = detail::k_dealer_router_request;
    zlink::message_t outbound = detail::make_message (sent);
    dealer.send (outbound);

    const router_callback_result_t router_result =
      detail::wait_future (router_result_future, 2000);
    assert (!router_result.routing_id.empty ());
    assert (router_result.payload == detail::k_dealer_router_request);

    zlink::message_t reply = detail::make_message (detail::k_dealer_router_reply);
    router.send (router_result.routing_id, reply);

    const dealer_callback_result_t dealer_result =
      detail::wait_future (dealer_result_future, 2000);
    assert (dealer_result.payload == detail::k_dealer_router_reply);
    std::printf ("[dealer-router/callback] send: \"%s\" → recv: \"%s\"\n",
                 sent.c_str (), dealer_result.payload.c_str ());
    return 0;
}
