import asyncio
import socket
import threading
import time
import unittest

import zlink


def _tcp_endpoint():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return f"tcp://127.0.0.1:{port}"


def _recv_text(sock):
    received = zlink.Received()
    sock.recv_into(received)
    with received:
        return received.single_part_or_throw().to_bytes()


CHANNEL_NAME = "spot-svc"
TOPIC = b"room:lobby"


class CoreApiAlignmentTests(unittest.TestCase):
    def test_routing_id_from_string_parses_hex(self):
        rid = zlink.RoutingId.from_bytes(bytes([0x00, 0x41, 0x42]))
        self.assertEqual(zlink.RoutingId.from_string("004142"), rid)
        self.assertEqual(str(rid), "004142")
        self.assertEqual(zlink.RoutingId.from_string("a" * 510).size, 255)
        with self.assertRaises(ValueError):
            zlink.RoutingId.from_string("not-hex")
        with self.assertRaises(ValueError):
            zlink.RoutingId.from_string("a" * 512)

    def test_public_surface_uses_canonical_names_only(self):
        self.assertFalse(hasattr(zlink, "SendResult"))
        self.assertFalse(hasattr(zlink, "SendFlag"))
        self.assertFalse(hasattr(zlink, "ReceiveFlag"))
        self.assertFalse(hasattr(zlink, "errno"))
        self.assertFalse(hasattr(zlink.PairSocket, "try_send"))
        self.assertFalse(hasattr(zlink.PairSocket, "try_recv"))
        self.assertFalse(hasattr(zlink.PairSocket, "on_receive"))
        self.assertFalse(hasattr(zlink.DealerSocket, "on_receive"))
        self.assertFalse(hasattr(zlink.RouterSocket, "on_receive"))
        self.assertFalse(hasattr(zlink.PubSocket, "try_publish"))
        self.assertFalse(hasattr(zlink.SubSocket, "try_subscribe"))
        self.assertFalse(hasattr(zlink.XPubSocket, "try_receive_subscription_event"))
        self.assertFalse(hasattr(zlink.SubSocket, "on_subscribe"))
        self.assertFalse(hasattr(zlink.XSubSocket, "on_subscribe"))
        self.assertFalse(hasattr(zlink.MonitorSocket, "try_recv"))
        self.assertFalse(hasattr(zlink, "ServiceMonitor"))
        self.assertFalse(hasattr(zlink.Spot, "try_subscribe"))
        self.assertFalse(hasattr(zlink.Spot, "try_publish"))
        self.assertFalse(hasattr(zlink.Spot, "on_subscribe"))
        self.assertTrue(hasattr(zlink.StreamSocket, "on_packet"))
        self.assertTrue(hasattr(zlink.Spot, "send_channel"))
        self.assertTrue(hasattr(zlink.Spot, "request_channel"))
        self.assertFalse(hasattr(zlink.DealerSocket, "request_callback"))
        self.assertFalse(hasattr(zlink.RouterSocket, "request_callback"))
        self.assertFalse(hasattr(zlink.RouterSocket, "request_to_spot_callback"))
        self.assertFalse(hasattr(zlink.Spot, "receive_subscription_event"))
        self.assertTrue(hasattr(zlink.Spot, "receive_subscription_event_into"))
        self.assertTrue(hasattr(zlink.SpotNode, "attach_channel_dealer"))
        self.assertTrue(hasattr(zlink.SpotNode, "attach_channel_dealer_manual"))
        self.assertTrue(hasattr(zlink.SpotNode, "attach_pub_ingress"))
        self.assertTrue(hasattr(zlink.SpotNode, "set_router_bind_endpoint"))
        self.assertTrue(hasattr(zlink.SpotNode, "connect_router_channel_peer"))
        self.assertTrue(hasattr(zlink.SpotNode, "disconnect_router_channel_peer"))
        self.assertTrue(hasattr(zlink.SpotNode, "disconnect_router_channel_peer_rid"))
        self.assertTrue(hasattr(zlink.SpotNode, "attach_spot_route_channel_discovery"))
        self.assertTrue(hasattr(zlink.PairSocket, "disconnect_rid"))
        self.assertTrue(hasattr(zlink.StreamSocket, "disconnect_rid"))
        self.assertTrue(hasattr(zlink.SpotNode, "disconnect_peer_rid"))
        self.assertTrue(hasattr(zlink, "SendFlags"))
        self.assertTrue(hasattr(zlink, "RecvFlags"))
        self.assertTrue(hasattr(zlink, "SubmitResult"))
        self.assertTrue(hasattr(zlink, "RequestResult"))
        self.assertTrue(hasattr(zlink, "RecvResult"))
        self.assertTrue(hasattr(zlink, "SpotDispatchEvent"))
        self.assertTrue(hasattr(zlink, "SpotDispatchSubjectKind"))
        self.assertTrue(hasattr(zlink, "SpotPeerKind"))
        self.assertTrue(hasattr(zlink.SpotDispatchEvent, "ACTOR_READABLE"))
        self.assertTrue(hasattr(zlink.SpotDispatchEvent, "ACTOR_JOIN_READABLE"))
        self.assertTrue(hasattr(zlink.SpotDispatchSubjectKind, "ACTOR"))
        self.assertTrue(hasattr(zlink, "SpotServiceAttachmentRole"))
        self.assertTrue(hasattr(zlink, "MonitorEvent"))
        self.assertTrue(hasattr(zlink, "MonitorEventMask"))
        self.assertTrue(hasattr(zlink, "Poller"))
        self.assertTrue(hasattr(zlink, "PollEvents"))
        self.assertTrue(hasattr(zlink, "PollEvent"))
        self.assertTrue(hasattr(zlink, "PollSourceKind"))
        self.assertTrue(hasattr(zlink.Poller, "wait"))
        self.assertFalse(hasattr(zlink.Poller, "poll"))
        self.assertEqual(zlink.MonitorEventMask.PEER_WEIGHT_CHANGED.value, 1 << 15)
        self.assertIs(zlink.SocketMonitorEvent, zlink.MonitorEvent)
        self.assertTrue(hasattr(zlink, "Actor"))
        self.assertTrue(hasattr(zlink, "ActorRef"))
        self.assertTrue(hasattr(zlink, "remote_actor_ref"))
        self.assertTrue(hasattr(zlink.SpotNode, "actor"))
        self.assertTrue(hasattr(zlink.SpotNode, "actor_lookup"))
        self.assertTrue(hasattr(zlink.SpotNode, "destroy_actor"))
        self.assertFalse(hasattr(zlink.SpotNode, "destroy_" + "remote_actor"))
        self.assertTrue(hasattr(zlink.SpotNode, "join_actor"))
        self.assertTrue(hasattr(zlink.SpotNode, "leave_actor"))
        self.assertTrue(hasattr(zlink.SpotNode, "get_or_create_spot"))
        self.assertTrue(hasattr(zlink.SpotNode, "spots_snapshot"))
        self.assertTrue(hasattr(zlink.SpotNode, "actors_snapshot"))
        self.assertTrue(hasattr(zlink.Spot, "recv_actor_join"))
        self.assertTrue(hasattr(zlink.Spot, "reply_actor_join"))
        self.assertTrue(hasattr(zlink.Spot, "actors_snapshot"))
        self.assertTrue(hasattr(zlink.StreamSocket, "bind_actor"))
        self.assertTrue(hasattr(zlink.StreamSocket, "unbind_actor"))
        self.assertTrue(hasattr(zlink.StreamSocket, "attach_actor_gateway"))
        self.assertTrue(hasattr(zlink.StreamSocket, "send_bound_actor"))
        self.assertTrue(hasattr(zlink.Discovery, "resolve_actor"))
        self.assertTrue(hasattr(zlink.SpotDispatchInfo, "recv_actor_part"))
        self.assertTrue(hasattr(zlink.Actor, "ref"))
        self.assertTrue(hasattr(zlink.Actor, "join"))
        self.assertTrue(hasattr(zlink.Actor, "leave"))
        self.assertTrue(hasattr(zlink.Actor, "recv_part"))
        self.assertTrue(hasattr(zlink.Actor, "send_bound_session"))
        self.assertTrue(hasattr(zlink.Actor, "close_bound_session"))
        self.assertFalse(hasattr(zlink.Actor, "send_bound_session_" + "packet"))

        remote = zlink.remote_actor_ref(zlink.RoutingId(b"node"), "actor")
        self.assertTrue(remote.is_unchecked())
        self.assertEqual(remote.generation, 0)

    def test_context_options_use_snake_case(self):
        ctx = zlink.Context()

        with ctx:
            self.assertTrue(hasattr(ctx.options, "io_threads"))
            ctx.options.io_threads = 1
            self.assertEqual(ctx.options.io_threads, 1)
            ctx.options.max_sockets = 1024
            self.assertEqual(ctx.options.max_sockets, 1024)
            ctx.options.auto_hwm_profile = zlink.AutoHwmProfile.THROUGHPUT
            self.assertEqual(
                ctx.options.auto_hwm_profile, zlink.AutoHwmProfile.THROUGHPUT
            )
            self.assertEqual(ctx.options.auto_hwm_msg_unit_bytes, 0)
            ctx.options.auto_hwm_msg_unit_bytes = 64
            self.assertEqual(ctx.options.auto_hwm_msg_unit_bytes, 64)
            ctx.options.auto_hwm_msg_unit_bytes = 0
            self.assertEqual(ctx.options.auto_hwm_msg_unit_bytes, 0)
            with self.assertRaises(ValueError):
                ctx.options.auto_hwm_msg_unit_bytes = -1
            self.assertFalse(hasattr(ctx.options, "ioThreads"))
            self.assertFalse(hasattr(ctx.options, "maxSockets"))

    def test_common_socket_options_include_canonical_typed_accessors(self):
        ctx = zlink.Context()

        with ctx:
            with zlink.PairSocket(ctx) as sock:
                self.assertTrue(hasattr(sock, "get_channel_name"))
                self.assertTrue(hasattr(sock, "set_channel_name"))
                self.assertFalse(hasattr(sock.options, "auto_hwm_msg_unit_bytes"))
                options = sock.options
                self.assertIn("connect_timeout_ms", dir(options))
                self.assertIn("ipv6", dir(options))
                self.assertIn("tcp_no_delay", dir(options))
                self.assertIn("tcp_keepalive", dir(options))
                self.assertIn("heartbeat_interval_ms", dir(options))
                self.assertIn("heartbeat_ttl_ms", dir(options))
                self.assertIn("heartbeat_timeout_ms", dir(options))
                self.assertIn("max_msg_size", dir(options))
                self.assertIn("backlog", dir(options))
                self.assertIn("reconnect_interval_ms", dir(options))
                self.assertIn("reconnect_interval_max_ms", dir(options))

    def test_pair_send_and_recv_use_flags(self):
        ctx = zlink.Context()

        with ctx:
            with zlink.PairSocket(ctx) as sender:
                with zlink.PairSocket(ctx) as receiver:
                    endpoint = "inproc://py-canonical-pair"
                    sender.bind(endpoint)
                    receiver.connect(endpoint)
                    received = zlink.Received()
                    self.assertFalse(
                        receiver.recv_into(received, flags=zlink.RecvFlags.DONT_WAIT)
                    )
                    sender.send().message(b"payload").flags(zlink.SendFlags.NONE).submit()
                    self.assertTrue(
                        receiver.recv_into(received, flags=zlink.RecvFlags.NONE)
                    )
                    with received:
                        self.assertTrue(received.is_single_part())
                        self.assertEqual(received.first_part().to_bytes(), b"payload")
                        self.assertEqual(received.single_part_or_throw().to_bytes(), b"payload")

    def test_poller_wait_uses_reusable_event_buffer(self):
        ctx = zlink.Context()

        with ctx:
            with zlink.PairSocket(ctx) as sender:
                with zlink.PairSocket(ctx) as receiver:
                    endpoint = "inproc://py-poller-wait"
                    sender.bind(endpoint)
                    receiver.connect(endpoint)
                    with zlink.Poller() as poller:
                        poller.add_socket(receiver, zlink.PollEventFlag.POLLIN, 7)
                        sender.send().message(b"poller").submit()
                        events = zlink.PollEvents(4)
                        count = poller.wait(events, 1000)
                        self.assertEqual(count, 1)
                        self.assertEqual(events.ready_count, 1)
                        self.assertEqual(events.source_kind(0), zlink.PollSourceKind.SOCKET)
                        self.assertEqual(events.slot(0), 7)
                        self.assertTrue(
                            events.has_event(0, zlink.PollEventFlag.POLLIN)
                        )

    def test_poller_rejects_empty_event_buffer_and_negative_slot(self):
        with self.assertRaises(ValueError):
            zlink.PollEvents(0)
        with zlink.Poller() as poller:
            with self.assertRaises(ValueError):
                poller.add_fd(0, zlink.PollEventFlag.POLLIN, -1)

    def test_poller_capacity_preserves_remaining_ready_sources(self):
        ctx = zlink.Context()

        with ctx:
            with zlink.PairSocket(ctx) as sender1:
                with zlink.PairSocket(ctx) as receiver1:
                    with zlink.PairSocket(ctx) as sender2:
                        with zlink.PairSocket(ctx) as receiver2:
                            endpoint1 = f"inproc://py-poller-capacity-a-{id(self)}"
                            endpoint2 = f"inproc://py-poller-capacity-b-{id(self)}"
                            sender1.bind(endpoint1)
                            receiver1.connect(endpoint1)
                            sender2.bind(endpoint2)
                            receiver2.connect(endpoint2)

                            with zlink.Poller() as poller:
                                poller.add_socket(
                                    receiver1, zlink.PollEventFlag.POLLIN, 101
                                )
                                poller.add_socket(
                                    receiver2, zlink.PollEventFlag.POLLIN, 102
                                )
                                sender1.send().message(b"a").submit()
                                sender2.send().message(b"b").submit()

                                events = zlink.PollEvents(1)
                                count = poller.wait(events, 1000)
                                self.assertEqual(count, 1)
                                first_slot = events.slot(0)
                                self.assertIn(first_slot, (101, 102))
                                if first_slot == 101:
                                    self.assertEqual(_recv_text(receiver1), b"a")
                                else:
                                    self.assertEqual(_recv_text(receiver2), b"b")

                                count = poller.wait(events, 1000)
                                self.assertEqual(count, 1)
                                self.assertNotEqual(events.slot(0), first_slot)
                                if events.slot(0) == 101:
                                    self.assertEqual(_recv_text(receiver1), b"a")
                                else:
                                    self.assertEqual(_recv_text(receiver2), b"b")

    def test_poller_modify_remove_and_timeout_follow_core_semantics(self):
        ctx = zlink.Context()

        with ctx:
            with zlink.PairSocket(ctx) as sender:
                with zlink.PairSocket(ctx) as receiver:
                    endpoint = f"inproc://py-poller-modify-remove-{id(self)}"
                    sender.bind(endpoint)
                    receiver.connect(endpoint)
                    with zlink.Poller() as poller:
                        events = zlink.PollEvents(1)
                        poller.add_socket(receiver, zlink.PollEventFlag.POLLIN, 31)
                        poller.modify_socket(receiver, zlink.PollEventFlag(0))
                        sender.send().message(b"hidden").submit()
                        self.assertEqual(poller.wait(events, 20), 0)

                        poller.modify_socket(receiver, zlink.PollEventFlag.POLLIN)
                        self.assertEqual(poller.wait(events, 1000), 1)
                        self.assertEqual(events.slot(0), 31)
                        self.assertEqual(_recv_text(receiver), b"hidden")

                        poller.remove_socket(receiver)
                        sender.send().message(b"removed").submit()
                        self.assertEqual(poller.wait(events, 0), 0)

    def test_poller_distinguishes_timer_and_socket_in_same_buffer(self):
        ctx = zlink.Context()

        with ctx:
            with zlink.PairSocket(ctx) as sender:
                with zlink.PairSocket(ctx) as receiver:
                    with zlink.Timer() as timer:
                        endpoint = f"inproc://py-poller-timer-socket-{id(self)}"
                        sender.bind(endpoint)
                        receiver.connect(endpoint)
                        with zlink.Poller() as poller:
                            events = zlink.PollEvents(2)
                            poller.add_socket(receiver, zlink.PollEventFlag.POLLIN, 41)
                            poller.add_timer(timer, 42)
                            sender.send().message(b"socket").submit()
                            timer.start(5_000_000, 1)

                            saw_socket = False
                            saw_timer = False
                            deadline = time.monotonic() + 2.0
                            while (not saw_socket or not saw_timer) and time.monotonic() < deadline:
                                count = poller.wait(events, 200)
                                for index in range(count):
                                    if events.source_kind(index) == zlink.PollSourceKind.SOCKET:
                                        self.assertEqual(events.slot(index), 41)
                                        self.assertEqual(_recv_text(receiver), b"socket")
                                        saw_socket = True
                                    elif events.source_kind(index) == zlink.PollSourceKind.TIMER:
                                        self.assertEqual(events.slot(index), 42)
                                        self.assertEqual(timer.recv(), 1)
                                        saw_timer = True

                            self.assertTrue(saw_socket)
                            self.assertTrue(saw_timer)

    def test_nonblocking_send_raises_submit_error(self):
        ctx = zlink.Context()

        with ctx:
            with zlink.RouterSocket(ctx) as router:
                router.options.mandatory = True
                with self.assertRaises(zlink.SubmitError) as cm:
                    router.send(b"UNKNOWN").message(b"payload").flags(
                        zlink.SendFlags.DONT_WAIT
                    ).submit()
                self.assertEqual(cm.exception.result, zlink.SubmitResult.NOT_CONNECTED)

    def test_pubsub_canonical_roundtrip(self):
        ctx = zlink.Context()

        with ctx:
            with zlink.PubSocket(ctx) as publisher:
                with zlink.SubSocket(ctx) as subscriber:
                    endpoint = "inproc://py-canonical-pubsub"
                    publisher.bind(endpoint)
                    subscriber.connect(endpoint)
                    subscriber.set_subscription(b"topic")
                    publisher.publish(b"topic").message(b"payload").flags(
                        zlink.SendFlags.NONE
                    ).submit()
                    received = zlink.TopicMessage()
                    self.assertTrue(
                        subscriber.subscribe_into(received, flags=zlink.RecvFlags.NONE)
                    )
                    with received:
                        self.assertEqual(received.topic, "topic")
                        self.assertEqual(received.single_part_or_throw().to_bytes(), b"payload")

    def test_monitor_surface_uses_recv_and_snapshot(self):
        ctx = zlink.Context()

        with ctx:
            with zlink.PairSocket(ctx) as sock:
                with sock.monitor_open() as monitor:
                    self.assertFalse(hasattr(monitor, "try_recv"))
                    self.assertTrue(hasattr(monitor, "ignore_handler"))
                    snapshot = monitor.snapshot()
                    self.assertIsInstance(snapshot, zlink.MonitorSnapshot)
                    self.assertTrue(hasattr(snapshot, "is_ready"))
                    self.assertIsInstance(snapshot.is_ready(), bool)
                    self.assertTrue(hasattr(snapshot, "auto_hwm_profile"))
                    self.assertTrue(hasattr(snapshot, "auto_hwm_policy_class"))
                    self.assertTrue(hasattr(snapshot, "auto_hwm_unit_budget_bytes"))
                    self.assertTrue(hasattr(snapshot, "auto_hwm_size_cap"))
                    self.assertTrue(
                        hasattr(snapshot, "auto_hwm_socket_message_slots")
                    )

    def test_request_reply_canonical_roundtrip(self):
        ctx = zlink.Context()

        async def scenario():
            with zlink.RouterSocket(ctx) as router_socket:
                self.assertTrue(hasattr(router_socket, "request"))
                self.assertFalse(hasattr(router_socket, "request_callback"))
                self.assertTrue(hasattr(router_socket, "reply"))
                self.assertTrue(hasattr(router_socket, "send_to_spot"))
                self.assertTrue(hasattr(router_socket, "request_to_spot"))
                self.assertFalse(hasattr(router_socket, "request_to_spot_callback"))
                self.assertTrue(hasattr(router_socket, "reply_to_spot"))
                self.assertFalse(hasattr(router_socket, "recv_spot"))
                self.assertFalse(hasattr(router_socket, "on_spot_receive"))
                with zlink.DealerSocket(ctx) as dealer_socket:
                    endpoint = "inproc://py-canonical-request-reply"
                    router_socket.bind(endpoint)
                    dealer_socket.connect(endpoint)

                    def responder():
                        received = zlink.Received()
                        self.assertTrue(router_socket.recv_into(received))
                        with received:
                            received.reply().message(b"pong").submit()

                    threading.Thread(target=responder, daemon=True).start()
                    reply = await dealer_socket.request().message(b"ping").timeout(2.0).submit_async()
                    try:
                        self.assertEqual([part.to_bytes() for part in reply], [b"pong"])
                    finally:
                        for part in reply:
                            part.close()

        asyncio.run(scenario())

    def test_spot_surface_and_pubsub_roundtrip(self):
        ctx = zlink.Context()

        with ctx:
            with zlink.SpotNode(ctx) as node:
                with self.assertRaises(TypeError):
                    zlink.Spot(node)
                node.set_routing_id(b"node-1")
                self.assertEqual(node.routing_id, zlink.RoutingId.from_bytes(b"node-1"))
                with node.create_spot() as spot:
                    self.assertIsInstance(spot, zlink.Spot)
                    self.assertTrue(hasattr(spot, "on_dispatch_event"))
                    self.assertFalse(
                        hasattr(spot, "drain_channel_reply_from"),
                        "channel reply progress is now driven by the per-spot"
                        " poller; explicit drain must not be on the public API",
                    )
                    spot.set_routing_id(b"spot-1")
                    self.assertEqual(spot.routing_id, zlink.RoutingId.from_bytes(b"spot-1"))
                    room_rid = zlink.RoutingId.from_bytes(b"py-room")
                    room_a, created_a = node.get_or_create_spot(room_rid)
                    room_b, created_b = node.get_or_create_spot(room_rid)
                    try:
                        self.assertTrue(created_a)
                        self.assertFalse(created_b)
                        self.assertEqual(room_a.routing_id, room_rid)
                        self.assertEqual(room_b.routing_id, room_rid)
                    finally:
                        room_b.close()
                        room_a.close()
                    self.assertTrue(hasattr(spot, "send_to_spot"))
                    self.assertTrue(hasattr(spot, "request_to_spot"))
                    self.assertTrue(hasattr(spot, "reply_to_spot"))
                    self.assertTrue(hasattr(spot, "reply_to_router"))
                    self.assertTrue(hasattr(spot, "recv_routed"))
                    self.assertTrue(hasattr(spot, "recv_routed_into"))
                    self.assertTrue(hasattr(spot, "on_routed_receive"))
                    self.assertTrue(hasattr(spot, "on_dispatch_event"))
                    self.assertTrue(hasattr(spot, "send_channel"))
                    self.assertTrue(hasattr(spot, "request_channel"))
                    self.assertFalse(hasattr(spot, "receive_subscription_event"))
                    self.assertTrue(hasattr(spot, "receive_subscription_event_into"))
                    self.assertFalse(hasattr(spot, "on_subscribe"))

            with zlink.SpotNode(ctx) as loopback_node:
                with loopback_node.create_spot() as loopback_spot:
                    loopback_spot.set_subscription(TOPIC)
                    subjects = loopback_node.subjects_snapshot()
                    self.assertTrue(
                        any(entry.subject == TOPIC.decode("utf-8") for entry in subjects)
                    )

    def test_discovery_surface_exposes_resolution_without_dealer_peer_mode(self):
        ctx = zlink.Context()

        with ctx:
            with zlink.Discovery(
                ctx, zlink.AutoConnectType.CLIENT_SERVER, "svc"
            ) as discovery:
                self.assertTrue(hasattr(discovery, "resolve_spot"))
                self.assertTrue(hasattr(discovery, "spot_owner_sync_enabled"))
                self.assertTrue(hasattr(type(discovery), "actor_route_sync_enabled"))
                self.assertFalse(hasattr(discovery, "set_dealer_peer_mode"))
                self.assertFalse(hasattr(zlink, "DiscoveryDealerPeerMode"))

    def test_message_and_routing_id_helpers_use_canonical_names(self):
        ctx = zlink.Context()
        ctx.close()

        rid = zlink.RoutingId.from_bytes(b"peer-1")
        self.assertEqual(rid.to_bytes(), b"peer-1")
        self.assertEqual(rid.size, 6)
        self.assertEqual(str(rid), rid.to_hex())

    def test_message_does_not_expose_borrowed_wrap_surface(self):
        self.assertFalse(hasattr(zlink.Message, "wrap_buffer"))
        self.assertTrue(hasattr(zlink.Message, "_wrap_buffer"))

    def test_async_request_surface_rejects_flags_without_callback(self):
        ctx = zlink.Context()

        with ctx:
            with zlink.DealerSocket(ctx) as dealer:
                op = dealer.request().message(b"payload").flags(zlink.SendFlags.NONE)
                self.assertFalse(hasattr(op, "submit_async"))
            with zlink.RouterSocket(ctx) as router:
                peer_rid = zlink.RoutingId.from_bytes(b"peer")
                spot_rid = zlink.RoutingId.from_bytes(b"spot")
                op = router.request(peer_rid).message(b"payload").flags(
                    zlink.SendFlags.NONE
                )
                self.assertFalse(hasattr(op, "submit_async"))
                op = router.request_to_spot(peer_rid, spot_rid).message(b"payload").flags(
                    zlink.SendFlags.NONE
                )
                self.assertFalse(hasattr(op, "submit_async"))

    def test_pub_manual_getter_surfaces_config_error(self):
        class _BrokenSocket:
            def _get_pub_bool_option(self, _option):
                raise zlink.ConfigError(zlink.ConfigResult.NOT_SUPPORTED, 0)

            def _set_pub_bool_option(self, _option, _enabled):
                raise AssertionError("setter should not be called")

        options = zlink.PubSocketOptions(_BrokenSocket())
        with self.assertRaises(zlink.ConfigError) as cm:
            _ = options.manual
        self.assertEqual(cm.exception.result, zlink.ConfigResult.NOT_SUPPORTED)

        with zlink.Message.from_bytes(b"payload") as message:
            self.assertEqual(message.size(), 7)
            self.assertEqual(message.to_bytes(), b"payload")
            self.assertEqual(bytes(message.data), b"payload")
            self.assertEqual(message.ref_count(), message.refCount())

        with zlink.Message.allocate(3) as message:
            data = message.data
            data[0] = 0x01
            data[1] = 0x02
            data[2] = 0x03
            self.assertEqual(message.to_bytes(), b"\x01\x02\x03")

    def test_c_string_inputs_reject_embedded_nul(self):
        ctx = zlink.Context()

        with ctx:
            with zlink.PubSocket(ctx) as publisher:
                with self.assertRaises(ValueError):
                    publisher.publish(b"bad\0topic").message(b"payload").submit()

            with zlink.SubSocket(ctx) as subscriber:
                with self.assertRaises(ValueError):
                    subscriber.set_subscription(b"bad\0topic")

            with zlink.SpotNode(ctx) as node:
                with node.create_spot() as spot:
                    with self.assertRaises(ValueError):
                        spot.publish(b"bad\0topic").message(b"payload").submit()

            with zlink.PairSocket(ctx) as pair:
                with self.assertRaises(ValueError):
                    pair.bind("tcp://127.0.0.1\0:5555")

    def test_utils_exports_and_minimal_behavior(self):
        self.assertIsInstance(zlink.version(), tuple)
        self.assertEqual(len(zlink.version()), 3)
        self.assertFalse(hasattr(zlink, "errno"))
        self.assertIsInstance(zlink.strerror(0), str)
        self.assertIsInstance(zlink.has("tls"), bool)

        with zlink.AtomicCounter() as counter:
            counter.set(3)
            self.assertEqual(counter.value, 3)
            self.assertEqual(counter.increment(), 3)
            self.assertEqual(counter.decrement(), 1)

        with zlink.Stopwatch() as watch:
            self.assertGreaterEqual(watch.intermediate(), 0)
            self.assertGreaterEqual(watch.stop(), 0)

        done = threading.Event()

        def _target():
            done.set()

        thread = zlink.Thread(_target)
        thread.join()
        self.assertTrue(done.is_set())

        with zlink.Timer() as timer:
            self.assertIsNotNone(timer)


if __name__ == "__main__":
    unittest.main()
