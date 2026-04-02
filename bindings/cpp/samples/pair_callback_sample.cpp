/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.hpp"

namespace {

struct callback_result_t
{
    std::string payload;
};

void pair_callback (const zlink_routing_id_t *,
                    zlink_msg_t *parts_,
                    size_t part_count_,
                    void *userdata_)
{
    std::promise<callback_result_t> *result_promise =
      static_cast<std::promise<callback_result_t> *> (userdata_);
    assert (result_promise != NULL);
    assert (part_count_ == 1);

    callback_result_t result;
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
    zlink::pair_socket_t server (ctx);
    zlink::pair_socket_t client (ctx);
    zlink::monitor_handle_t server_monitor = server.monitor_handle ();
    zlink::monitor_handle_t client_monitor = client.monitor_handle ();

    assert (server.bind ("tcp://127.0.0.1:0") == 0);
    std::string endpoint;
    assert (server.get_option (zlink::socket_options::last_endpoint, endpoint)
            == 0);
    assert (!endpoint.empty ());
    assert (client.connect (endpoint) == 0);
    assert (detail::wait_connected (server_monitor, client_monitor));

    std::promise<callback_result_t> result_promise;
    std::future<callback_result_t> result_future = result_promise.get_future ();
    assert (server.on_receive (&pair_callback, &result_promise) == 0);

    const std::string sent = detail::k_pair_payload;
    zlink::message_t outbound = detail::make_message (sent);
    client.send (outbound);

    const callback_result_t result = detail::wait_future (result_future, 2000);
    assert (result.payload == detail::k_pair_payload);
    std::printf ("[pair/callback] send: \"%s\" → recv: \"%s\"\n",
                 sent.c_str (), result.payload.c_str ());
    return 0;
}
