import queue
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
    received = zlink.create_received()
    sock.recv_into(received)
    with received:
        return received.single_part_or_throw().to_bytes()


CHANNEL_NAME = "spot-svc"
TOPIC = b"room:lobby"


class CoreApiAlignmentTests(unittest.TestCase):
    def test_routing_id_hex_and_display_policy(self):
        rid = zlink.RoutingId.from_(bytes([0x00, 0x41, 0x42]))
        self.assertEqual(zlink.RoutingId.from_hex("004142"), rid)
        self.assertEqual(str(rid), "hex:004142")
        self.assertEqual(zlink.RoutingId.from_hex("a" * 510).size, 255)
        with self.assertRaises(ValueError):
            zlink.RoutingId.from_hex("not-hex")
        with self.assertRaises(ValueError):
            zlink.RoutingId.from_hex("a" * 512)
        self.assertEqual(str(zlink.RoutingId.from_("dealer-1")), "dealer-1")
        self.assertEqual(str(zlink.RoutingId.from_(23)), "23")

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
        self.assertFalse(hasattr(zlink.XPubSocket, "receive_subscription_event"))
        self.assertTrue(hasattr(zlink.XPubSocket, "receive_subscription_event_into"))
        self.assertTrue(hasattr(zlink.SubSocket, "subscription_at"))
        self.assertTrue(hasattr(zlink.XSubSocket, "subscription_at"))
        self.assertFalse(hasattr(zlink.SubSocket, "on_subscribe"))
        self.assertFalse(hasattr(zlink.XSubSocket, "on_subscribe"))
        with zlink.create_context() as ctx:
            with zlink.create_pub_socket(ctx) as pub:
                self.assertTrue(hasattr(pub, "pub_options"))
                self.assertFalse(hasattr(pub, "publisher_options"))
            with zlink.create_sub_socket(ctx) as sub:
                self.assertTrue(hasattr(sub, "sub_options"))
                self.assertFalse(hasattr(sub, "subscriber_options"))
            with zlink.create_xpub_socket(ctx) as xpub:
                self.assertTrue(hasattr(xpub, "pub_options"))
                self.assertFalse(hasattr(xpub, "publisher_options"))
            with zlink.create_xsub_socket(ctx) as xsub:
                self.assertTrue(hasattr(xsub, "sub_options"))
                self.assertFalse(hasattr(xsub, "subscriber_options"))
        self.assertFalse(hasattr(zlink.MonitorSocket, "try_recv"))
        self.assertFalse(hasattr(zlink, "ServiceMonitor"))
        self.assertFalse(hasattr(zlink.Spot, "try_subscribe"))
        self.assertFalse(hasattr(zlink.Spot, "try_publish"))
        self.assertFalse(hasattr(zlink.Spot, "on_subscribe"))
        self.assertTrue(hasattr(zlink.StreamSocket, "on_packet"))
        self.assertTrue(hasattr(zlink.Spot, "send_to_channel"))
        self.assertTrue(hasattr(zlink.Spot, "request_to_channel"))
        self.assertFalse(hasattr(zlink.Spot, "send_channel"))
        self.assertFalse(hasattr(zlink.Spot, "request_channel"))
        self.assertFalse(hasattr(zlink.DealerSocket, "request_callback"))
        self.assertFalse(hasattr(zlink.RouterSocket, "request_callback"))
        self.assertFalse(hasattr(zlink.RouterSocket, "request_to_spot_callback"))
        self.assertFalse(hasattr(zlink.Spot, "receive_subscription_event"))
        self.assertTrue(hasattr(zlink.Spot, "receive_subscription_event_into"))
        self.assertTrue(hasattr(zlink.SpotNode, "create_route_bridge"))
        self.assertTrue(hasattr(zlink.SpotNode, "create_publisher"))
        self.assertTrue(hasattr(zlink.SpotRouteBridge, "attach_router_channel"))
        self.assertTrue(hasattr(zlink.SpotRouteBridge, "send"))
        self.assertTrue(hasattr(zlink.SpotRouteBridge, "request"))
        self.assertTrue(hasattr(zlink.SpotNodePublisher, "publish"))
        self.assertTrue(hasattr(zlink.SpotNode, "set_pub_bind"))
        self.assertTrue(hasattr(zlink.SpotNode, "set_router_bind"))
        self.assertTrue(hasattr(zlink.SpotNode, "set_router_high_water_mark"))
        self.assertTrue(hasattr(zlink.SpotNode, "set_pubsub_high_water_mark"))
        self.assertTrue(hasattr(zlink.SpotNode, "router_high_water_mark"))
        self.assertTrue(hasattr(zlink.SpotNode, "pubsub_high_water_mark"))
        self.assertFalse(hasattr(zlink.SpotNode, "set_router_hwm"))
        self.assertFalse(hasattr(zlink.SpotNode, "set_pubsub_hwm"))
        self.assertFalse(hasattr(zlink.SpotNode, "router_hwm"))
        self.assertFalse(hasattr(zlink.SpotNode, "pubsub_hwm"))
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
        self.assertFalse(hasattr(zlink, "SpotServiceAttachmentRole"))
        self.assertTrue(hasattr(zlink, "MonitorEvent"))
        self.assertTrue(hasattr(zlink, "MonitorEventMask"))
        self.assertTrue(hasattr(zlink, "Poller"))
        self.assertTrue(hasattr(zlink, "PollEvents"))
        self.assertTrue(hasattr(zlink, "PollEvent"))
        self.assertTrue(hasattr(zlink, "PollSourceKind"))
        self.assertTrue(hasattr(zlink.Poller, "wait"))
        self.assertFalse(hasattr(zlink.Poller, "poll"))
        self.assertTrue(hasattr(zlink.Stopwatch, "intermediate"))
        self.assertFalse(hasattr(zlink.Stopwatch, "elapsed_ns"))
        self.assertFalse(hasattr(zlink.Stopwatch, "start"))
        self.assertFalse(hasattr(zlink.Thread, "start"))
        self.assertEqual(zlink.MonitorEventMask.PEER_WEIGHT_CHANGED.value, 1 << 15)
        self.assertFalse(hasattr(zlink, "SocketMonitorEvent"))
        self.assertTrue(hasattr(zlink, "Actor"))
        self.assertTrue(hasattr(zlink, "ActorRef"))
        self.assertTrue(hasattr(zlink, "remote_actor_ref"))
        self.assertTrue(hasattr(zlink.SpotNode, "actor"))
        self.assertTrue(hasattr(zlink.SpotNode, "actor_lookup"))
        self.assertTrue(hasattr(zlink.SpotNode, "destroy_actor"))
        self.assertFalse(hasattr(zlink.SpotNode, "destroy_" + "remote_actor"))
        self.assertTrue(hasattr(zlink.SpotNode, "send_to_actor"))
        self.assertTrue(hasattr(zlink.SpotNode, "request_to_actor"))
        self.assertTrue(hasattr(zlink.SpotNode, "join_actor"))
        self.assertTrue(hasattr(zlink.SpotNode, "leave_actor"))
        self.assertTrue(hasattr(zlink.SpotNode, "get_or_create_spot"))
        self.assertTrue(hasattr(zlink.SpotNode, "spots"))
        self.assertTrue(hasattr(zlink.SpotNode, "actors"))
        self.assertTrue(hasattr(zlink.Spot, "recv_actor_join"))
        self.assertTrue(hasattr(zlink.Spot, "reply_actor_join"))
        self.assertTrue(hasattr(zlink.Spot, "actors"))
        self.assertTrue(hasattr(zlink.StreamSocket, "bind_actor"))
        self.assertTrue(hasattr(zlink.StreamSocket, "unbind_actor"))
        self.assertTrue(hasattr(zlink.StreamSocket, "send_bound_actor"))
        self.assertFalse(hasattr(zlink, "Discovery"))
        self.assertFalse(hasattr(zlink, "create_discovery"))
        self.assertFalse(hasattr(zlink.DealerSocket, "attach_discovery"))
        self.assertFalse(hasattr(zlink.RouterSocket, "attach_discovery"))
        self.assertFalse(hasattr(zlink.PubSocket, "attach_discovery"))
        self.assertFalse(hasattr(zlink.SubSocket, "attach_discovery"))
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

    def test_spot_route_bridge_router_send_emits_relay_packet(self):
        with zlink.create_context() as ctx, \
             zlink.create_spot_node(ctx) as node, \
             zlink.create_router_socket(ctx) as bridge_router, \
             zlink.create_router_socket(ctx) as router:
            with node.create_route_bridge() as bridge:
                endpoint = f"inproc://python-spot-route-bridge-{time.time_ns()}"
                bridge_router.set_routing_id(b"python-bridge-client")
                router.set_routing_id(b"python-bridge-server")
                router.bind(endpoint)
                bridge_router.connect(endpoint)
                time.sleep(0.05)
                bridge.attach_router_channel("api", bridge_router)

                self.assertTrue(
                    bridge.send("api", b"python-bridge-server", b"target-spot")
                    .message(b"hello-bridge")
                    .submit()
                )

                received = zlink.create_received()
                try:
                    deadline = time.time() + 3
                    while True:
                        if router.recv_into(received, flags=zlink.RecvFlags.DONT_WAIT):
                            break
                        if time.time() > deadline:
                            self.fail("timed out waiting for bridge relay packet")
                        time.sleep(0.001)
                    parts = received.to_bytes_list()
                    self.assertGreaterEqual(len(parts), 3)
                    self.assertEqual(parts[0], b"__zlink.routed_spot.egress.send")
                    self.assertEqual(parts[1], b"target-spot")
                    self.assertEqual(parts[2].rstrip(b"\0"), b"hello-bridge")
                finally:
                    received.close()

    def test_protocol_contracts_do_not_inject_runtime_stub_methods(self):
        with zlink.create_stopwatch() as watch:
            self.assertFalse(hasattr(watch, "start"))
            self.assertFalse(hasattr(watch, "elapsed_ns"))
            self.assertGreaterEqual(watch.intermediate(), 0)

        thread = zlink.create_thread(lambda: None)
        self.assertFalse(hasattr(thread, "start"))
        thread.join()

    def test_entry_spot_join_accepts_and_rejects_with_reply(self):
        with zlink.create_context() as ctx:
            with zlink.create_spot_node(ctx) as node:
                node.set_routing_id(zlink.RoutingId.from_(b"py-entry-node"))
                user_spot = node.create_spot()
                entry_spot = node.entry_spot()
                actor = node.actor("py-entry-actor")
                reject_entry = {"value": False}
                user_requests = queue.Queue()
                entry_requests = queue.Queue()

                try:
                    def on_user_dispatch(spot, info):
                        if info.event != zlink.SpotDispatchEvent.ACTOR_JOIN_READABLE:
                            return
                        request = spot.recv_actor_join(flags=zlink.RecvFlags.DONT_WAIT)
                        if request is None:
                            return
                        user_requests.put(request.message.to_bytes())
                        spot.reply_actor_join(request, 0).message(b"user-reply").submit()

                    def on_entry_dispatch(spot, info):
                        if info.event != zlink.SpotDispatchEvent.ACTOR_JOIN_READABLE:
                            return
                        request = spot.recv_actor_join(flags=zlink.RecvFlags.DONT_WAIT)
                        if request is None:
                            return
                        entry_requests.put(request.message.to_bytes())
                        if reject_entry["value"]:
                            spot.reply_actor_join(request, 7).message(b"entry-rejected").submit()
                        else:
                            spot.reply_actor_join(request, 0).message(b"entry-reply").submit()

                    user_spot.on_dispatch_event(on_user_dispatch)
                    entry_spot.on_dispatch_event(on_entry_dispatch)

                    user_queue = queue.Queue()
                    actor.join(user_spot).message(b"user-join-request").timeout(1.0).submit(
                        lambda result, parts: user_queue.put((result, parts))
                    )
                    user_result, user_parts = user_queue.get(timeout=2.0)
                    self.assertEqual(user_result.result, zlink.RequestResult.OK)
                    self.assertEqual(user_requests.get(timeout=2.0), b"user-join-request")
                    for part in user_parts:
                        part.close()

                    entry_queue = queue.Queue()
                    node.join_actor_entry_spot(
                        actor.ref(),
                        node.routing_id,
                        b"entry-join-request",
                    ).timeout(1.0).submit(
                        lambda result, parts: entry_queue.put((result, parts))
                    )
                    entry_result, entry_parts = entry_queue.get(timeout=2.0)
                    self.assertEqual(entry_result.result, zlink.RequestResult.OK)
                    self.assertEqual(entry_result.join_result_code, 0)
                    self.assertEqual(entry_requests.get(timeout=2.0), b"entry-join-request")
                    try:
                        self.assertEqual([part.to_bytes() for part in entry_parts], [b"entry-reply"])
                    finally:
                        for part in entry_parts:
                            part.close()
                    current_actor = entry_result.actor

                    return_queue = queue.Queue()
                    node.join_actor(
                        current_actor,
                        node.routing_id,
                        user_spot.routing_id,
                    ).message(b"return-to-user").timeout(1.0).submit(
                        lambda result, parts: return_queue.put((result, parts))
                    )
                    return_result, return_parts = return_queue.get(timeout=2.0)
                    self.assertEqual(return_result.result, zlink.RequestResult.OK)
                    for part in return_parts:
                        part.close()
                    current_actor = return_result.actor

                    reject_entry["value"] = True
                    reject_queue = queue.Queue()
                    node.join_actor_entry_spot(
                        current_actor,
                        node.routing_id,
                        b"entry-reject-request",
                    ).timeout(1.0).submit(
                        lambda result, parts: reject_queue.put((result, parts))
                    )
                    rejected_result, rejected_parts = reject_queue.get(timeout=2.0)
                    self.assertEqual(rejected_result.result, zlink.RequestResult.OK)
                    self.assertEqual(rejected_result.join_result_code, 7)
                    self.assertEqual(entry_requests.get(timeout=2.0), b"entry-reject-request")
                    try:
                        self.assertEqual(
                            [part.to_bytes() for part in rejected_parts],
                            [b"entry-rejected"],
                        )
                    finally:
                        for part in rejected_parts:
                            part.close()
                    actors = user_spot.actors()
                    self.assertEqual(len(actors), 1)
                    self.assertEqual(actors[0].actor_id, current_actor.actor_id)

                    reject_entry["value"] = False
                    cleanup_queue = queue.Queue()
                    node.join_actor_entry_spot(
                        current_actor,
                        node.routing_id,
                        b"entry-cleanup-request",
                    ).timeout(1.0).submit(
                        lambda result, parts: cleanup_queue.put((result, parts))
                    )
                    cleanup_result, cleanup_parts = cleanup_queue.get(timeout=2.0)
                    self.assertEqual(cleanup_result.result, zlink.RequestResult.OK)
                    for part in cleanup_parts:
                        part.close()
                finally:
                    entry_spot.close()
                    user_spot.close()

    def test_router_reply_contract_matches_runtime_signature(self):
        ctx = zlink.create_context()

        with ctx:
            with zlink.create_router_socket(ctx) as router:
                op = router.reply(zlink.RoutingId.from_(b"peer"), 1)
                self.assertIsInstance(op, zlink.ReplyOp)

    def test_context_options_use_snake_case(self):
        ctx = zlink.create_context()

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
        ctx = zlink.create_context()

        with ctx:
            with zlink.create_pair_socket(ctx) as sock:
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
                self.assertIn("max_message_size", dir(options))
                self.assertIn("backlog", dir(options))
                self.assertIn("reconnect_interval_ms", dir(options))
                self.assertIn("reconnect_interval_max_ms", dir(options))

    def test_pair_send_and_recv_use_flags(self):
        ctx = zlink.create_context()

        with ctx:
            with zlink.create_pair_socket(ctx) as sender:
                with zlink.create_pair_socket(ctx) as receiver:
                    endpoint = "inproc://py-canonical-pair"
                    sender.bind(endpoint)
                    receiver.connect(endpoint)
                    received = zlink.create_received()
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

    def test_pair_send_keeps_builder_only_contract(self):
        ctx = zlink.create_context()

        with ctx:
            with zlink.create_pair_socket(ctx) as sender:
                with self.assertRaises(TypeError):
                    sender.send(b"direct", flags=zlink.SendFlags.DONT_WAIT)

    def test_poller_wait_uses_reusable_event_buffer(self):
        ctx = zlink.create_context()

        with ctx:
            with zlink.create_pair_socket(ctx) as sender:
                with zlink.create_pair_socket(ctx) as receiver:
                    endpoint = "inproc://py-poller-wait"
                    sender.bind(endpoint)
                    receiver.connect(endpoint)
                    with zlink.create_poller() as poller:
                        poller.add_socket(receiver, zlink.PollEventFlag.POLLIN, 7)
                        sender.send().message(b"poller").submit()
                        events = zlink.create_poll_events(4)
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
            zlink.create_poll_events(0)
        with zlink.create_poller() as poller:
            with self.assertRaises(ValueError):
                poller.add_fd(0, zlink.PollEventFlag.POLLIN, -1)

    def test_poller_capacity_preserves_remaining_ready_sources(self):
        ctx = zlink.create_context()

        with ctx:
            with zlink.create_pair_socket(ctx) as sender1:
                with zlink.create_pair_socket(ctx) as receiver1:
                    with zlink.create_pair_socket(ctx) as sender2:
                        with zlink.create_pair_socket(ctx) as receiver2:
                            endpoint1 = f"inproc://py-poller-capacity-a-{id(self)}"
                            endpoint2 = f"inproc://py-poller-capacity-b-{id(self)}"
                            sender1.bind(endpoint1)
                            receiver1.connect(endpoint1)
                            sender2.bind(endpoint2)
                            receiver2.connect(endpoint2)

                            with zlink.create_poller() as poller:
                                poller.add_socket(
                                    receiver1, zlink.PollEventFlag.POLLIN, 101
                                )
                                poller.add_socket(
                                    receiver2, zlink.PollEventFlag.POLLIN, 102
                                )
                                sender1.send().message(b"a").submit()
                                sender2.send().message(b"b").submit()

                                events = zlink.create_poll_events(1)
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
        ctx = zlink.create_context()

        with ctx:
            with zlink.create_pair_socket(ctx) as sender:
                with zlink.create_pair_socket(ctx) as receiver:
                    endpoint = f"inproc://py-poller-modify-remove-{id(self)}"
                    sender.bind(endpoint)
                    receiver.connect(endpoint)
                    with zlink.create_poller() as poller:
                        events = zlink.create_poll_events(1)
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
        ctx = zlink.create_context()

        with ctx:
            with zlink.create_pair_socket(ctx) as sender:
                with zlink.create_pair_socket(ctx) as receiver:
                    with zlink.create_timer() as timer:
                        endpoint = f"inproc://py-poller-timer-socket-{id(self)}"
                        sender.bind(endpoint)
                        receiver.connect(endpoint)
                        with zlink.create_poller() as poller:
                            events = zlink.create_poll_events(2)
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
        ctx = zlink.create_context()

        with ctx:
            with zlink.create_router_socket(ctx) as router:
                router.options.mandatory = True
                with self.assertRaises(zlink.SubmitError) as cm:
                    router.send(b"UNKNOWN").message(b"payload").flags(
                        zlink.SendFlags.DONT_WAIT
                    ).submit()
                self.assertEqual(cm.exception.result, zlink.SubmitResult.NOT_CONNECTED)

    def test_router_send_keeps_builder_only_contract(self):
        ctx = zlink.create_context()

        with ctx:
            with zlink.create_router_socket(ctx) as router:
                with self.assertRaises(TypeError):
                    router.send(
                        b"UNKNOWN", b"payload", flags=zlink.SendFlags.DONT_WAIT
                    )

    def test_pubsub_canonical_roundtrip(self):
        ctx = zlink.create_context()

        with ctx:
            with zlink.create_pub_socket(ctx) as publisher:
                with zlink.create_sub_socket(ctx) as subscriber:
                    endpoint = "inproc://py-canonical-pubsub"
                    publisher.bind(endpoint)
                    subscriber.connect(endpoint)
                    subscriber.set_subscription(b"topic")
                    publisher.publish(b"topic").message(b"payload").flags(
                        zlink.SendFlags.NONE
                    ).submit()
                    received = zlink.create_topic_message()
                    self.assertTrue(
                        subscriber.subscribe_into(received, flags=zlink.RecvFlags.NONE)
                    )
                    with received:
                        self.assertEqual(received.topic, "topic")
                        self.assertEqual(received.single_part_or_throw().to_bytes(), b"payload")

    def test_pubsub_publish_keeps_builder_only_contract(self):
        ctx = zlink.create_context()

        with ctx:
            with zlink.create_pub_socket(ctx) as publisher:
                with self.assertRaises(TypeError):
                    publisher.publish(
                        b"topic", b"direct", flags=zlink.SendFlags.DONT_WAIT
                    )

    def test_monitor_surface_uses_recv_and_status(self):
        ctx = zlink.create_context()

        with ctx:
            with zlink.create_pair_socket(ctx) as sock:
                with sock.monitor_open() as monitor:
                    self.assertFalse(hasattr(monitor, "try_recv"))
                    self.assertTrue(hasattr(monitor, "ignore_handler"))
                    status = monitor.status()
                    self.assertIsInstance(status, zlink.MonitorStatus)
                    self.assertTrue(hasattr(status, "is_ready"))
                    self.assertIsInstance(status.is_ready(), bool)
                    self.assertTrue(hasattr(status, "auto_hwm_profile"))
                    self.assertTrue(hasattr(status, "auto_hwm_policy_class"))
                    self.assertTrue(hasattr(status, "auto_hwm_unit_budget_bytes"))
                    self.assertTrue(hasattr(status, "auto_hwm_size_cap"))
                    self.assertTrue(
                        hasattr(status, "auto_hwm_socket_message_slots")
                    )
                    self.assertTrue(
                        hasattr(status, "auto_hwm_connection_bucket_count")
                    )
                    self.assertTrue(
                        hasattr(status, "auto_hwm_connection_bucket_hwm_4k")
                    )

    def test_request_reply_canonical_roundtrip(self):
        ctx = zlink.create_context()

        with zlink.create_router_socket(ctx) as router_socket:
            self.assertTrue(hasattr(router_socket, "request"))
            self.assertFalse(hasattr(router_socket, "request_callback"))
            self.assertTrue(hasattr(router_socket, "reply"))
            self.assertTrue(hasattr(router_socket, "send_to_spot"))
            self.assertTrue(hasattr(router_socket, "request_to_spot"))
            self.assertFalse(hasattr(router_socket, "request_to_spot_callback"))
            self.assertTrue(hasattr(router_socket, "reply_to_spot"))
            self.assertFalse(hasattr(router_socket, "recv_spot"))
            self.assertFalse(hasattr(router_socket, "on_spot_receive"))
            with zlink.create_dealer_socket(ctx) as dealer_socket:
                endpoint = "inproc://py-canonical-request-reply"
                router_socket.bind(endpoint)
                dealer_socket.connect(endpoint)

                def responder():
                    received = zlink.create_received()
                    self.assertTrue(router_socket.recv_into(received))
                    with received:
                        received.reply().message(b"pong").submit()

                reply_queue = queue.Queue()
                threading.Thread(target=responder, daemon=True).start()
                dealer_socket.request().message(b"ping").timeout(2.0).submit(
                    lambda result, parts: reply_queue.put((result, parts))
                )
                result, reply = reply_queue.get(timeout=2.0)
                self.assertEqual(result, zlink.RequestResult.OK)
                try:
                    self.assertEqual([part.to_bytes() for part in reply], [b"pong"])
                finally:
                    for part in reply:
                        part.close()

    def test_spot_surface_and_pubsub_roundtrip(self):
        ctx = zlink.create_context()

        with ctx:
            with zlink.create_spot_node(ctx) as node:
                with self.assertRaises(TypeError):
                    zlink.Spot(node)
                node.set_routing_id(b"node-1")
                self.assertEqual(node.routing_id, zlink.RoutingId.from_(b"node-1"))
                with node.create_spot() as spot:
                    self.assertIsInstance(spot, zlink.Spot)
                    self.assertTrue(hasattr(spot, "on_dispatch_event"))
                    self.assertFalse(
                        hasattr(spot, "drain_channel_reply_from"),
                        "channel reply progress is now driven by the per-spot"
                        " poller; explicit drain must not be on the public API",
                    )
                    spot.set_routing_id(b"spot-1")
                    self.assertEqual(spot.routing_id, zlink.RoutingId.from_(b"spot-1"))
                    room_rid = zlink.RoutingId.from_(b"py-room")
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
                    self.assertFalse(hasattr(spot, "on_routed_receive"))
                    self.assertTrue(hasattr(spot, "recv_actor_lifecycle"))
                    self.assertTrue(hasattr(spot, "on_dispatch_event"))
                    self.assertTrue(hasattr(spot, "send_to_channel"))
                    self.assertTrue(hasattr(spot, "request_to_channel"))
                    self.assertFalse(hasattr(spot, "send_channel"))
                    self.assertFalse(hasattr(spot, "request_channel"))
                    self.assertFalse(hasattr(spot, "receive_subscription_event"))
                    self.assertTrue(hasattr(spot, "receive_subscription_event_into"))
                    self.assertFalse(hasattr(spot, "on_subscribe"))

            with zlink.create_spot_node(ctx) as loopback_node:
                with loopback_node.create_spot() as loopback_spot:
                    loopback_spot.set_subscription(TOPIC)
                    subjects = loopback_node.subjects()
                    self.assertTrue(
                        any(entry.subject == TOPIC.decode("utf-8") for entry in subjects)
                    )

    def test_message_and_routing_id_helpers_use_canonical_names(self):
        ctx = zlink.create_context()
        ctx.close()

        rid = zlink.RoutingId.from_(b"peer-1")
        self.assertEqual(rid.to_bytes(), b"peer-1")
        self.assertEqual(rid.size, 6)
        self.assertEqual(str(rid), "peer-1")
        self.assertTrue(hasattr(zlink.Message, "from_"))
        self.assertFalse(hasattr(zlink.Message, "copy_from"))
        self.assertFalse(hasattr(zlink.Message, "from_bytes"))

    def test_message_projection_is_consistent_across_public_contract_imports(self):
        from zlink.contracts import Message as ContractMessage
        from zlink.contracts.messaging import Message as MessagingMessage
        from zlink.contracts.messaging.message import Message as MessageModuleMessage

        self.assertIs(ContractMessage, zlink.Message)
        self.assertIs(MessagingMessage, zlink.Message)
        self.assertIs(MessageModuleMessage, zlink.Message)
        with ContractMessage.from_(b"payload") as message:
            self.assertEqual(message.to_bytes(), b"payload")
        with MessagingMessage.from_("text") as message:
            self.assertEqual(message.to_bytes(), b"text")
        with MessageModuleMessage.from_(bytearray(b"module")) as message:
            self.assertEqual(message.to_bytes(), b"module")

    def test_message_does_not_expose_borrowed_wrap_surface(self):
        self.assertFalse(hasattr(zlink.Message, "wrap_buffer"))
        self.assertFalse(hasattr(zlink.Message, "_wrap_buffer"))

    def test_async_request_surface_rejects_flags_without_callback(self):
        ctx = zlink.create_context()

        with ctx:
            with zlink.create_dealer_socket(ctx) as dealer:
                op = dealer.request().message(b"payload").flags(zlink.SendFlags.NONE)
                self.assertFalse(hasattr(op, "submit_async"))
            with zlink.create_router_socket(ctx) as router:
                peer_rid = zlink.RoutingId.from_(b"peer")
                spot_rid = zlink.RoutingId.from_(b"spot")
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

        options = zlink.create_pub_socket_options(_BrokenSocket())
        with self.assertRaises(zlink.ConfigError) as cm:
            _ = options.manual
        self.assertEqual(cm.exception.result, zlink.ConfigResult.NOT_SUPPORTED)

    def test_message_diagnostics_and_copy_helpers(self):
        with zlink.Message.from_(b"payload") as message:
            self.assertEqual(message.size(), 7)
            self.assertEqual(message.to_bytes(), b"payload")
            self.assertEqual(bytes(message.data), b"payload")
            self.assertFalse(hasattr(message, "getProperty"))
            self.assertFalse(hasattr(message, "refCount"))
            self.assertGreaterEqual(message.ref_count(), 1)
            self.assertFalse(message.is_empty())
            self.assertEqual(message.to_string(), "payload")

            target = bytearray(8)
            self.assertEqual(message.copy_to(target, destination_offset=1), 7)
            self.assertEqual(target, b"\x00payload")
            self.assertTrue(message.try_copy_to(bytearray(7)))
            self.assertFalse(message.try_copy_to(bytearray(6)))

            with zlink.Message.from_(message) as copy:
                self.assertEqual(copy.to_bytes(), b"payload")
                self.assertIsNot(copy, message)

        with zlink.Message.from_("text") as message:
            self.assertEqual(message.to_bytes(), b"text")

        with zlink.allocate_message(3) as message:
            data = message.data
            data[0] = 0x01
            data[1] = 0x02
            data[2] = 0x03
            self.assertEqual(message.to_bytes(), b"\x01\x02\x03")
            self.assertFalse(message.is_empty())

        with zlink.allocate_message(0) as message:
            self.assertTrue(message.is_empty())

    def test_c_string_inputs_reject_embedded_nul(self):
        ctx = zlink.create_context()

        with ctx:
            with zlink.create_pub_socket(ctx) as publisher:
                with self.assertRaises(ValueError):
                    publisher.publish(b"bad\0topic").message(b"payload").submit()

            with zlink.create_sub_socket(ctx) as subscriber:
                with self.assertRaises(ValueError):
                    subscriber.set_subscription(b"bad\0topic")

            with zlink.create_spot_node(ctx) as node:
                with node.create_spot() as spot:
                    with self.assertRaises(ValueError):
                        spot.publish(b"bad\0topic").message(b"payload").submit()

            with zlink.create_pair_socket(ctx) as pair:
                with self.assertRaises(ValueError):
                    pair.bind("tcp://127.0.0.1\0:5555")

    def test_utils_exports_and_minimal_behavior(self):
        self.assertIsInstance(zlink.version(), tuple)
        self.assertEqual(len(zlink.version()), 3)
        self.assertFalse(hasattr(zlink, "errno"))
        self.assertIsInstance(zlink.strerror(0), str)
        self.assertIsInstance(zlink.has("tls"), bool)

        with zlink.create_atomic_counter() as counter:
            counter.set(3)
            self.assertEqual(counter.value, 3)
            self.assertEqual(counter.increment(), 3)
            self.assertEqual(counter.decrement(), 1)

        with zlink.create_stopwatch() as watch:
            self.assertGreaterEqual(watch.intermediate(), 0)
            self.assertGreaterEqual(watch.stop(), 0)

        done = threading.Event()

        def _target():
            done.set()

        thread = zlink.create_thread(_target)
        thread.join()
        self.assertTrue(done.is_set())

        with zlink.create_timer() as timer:
            self.assertIsNotNone(timer)


if __name__ == "__main__":
    unittest.main()
