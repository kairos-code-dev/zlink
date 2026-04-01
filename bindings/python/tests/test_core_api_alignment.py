import socket
import os
import subprocess
import sys
import threading
import unittest
import warnings
from pathlib import Path

import zlink


ROOT = Path(__file__).resolve().parents[1]
EXAMPLES_DIR = ROOT / "examples"


def _tcp_endpoint():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return f"tcp://127.0.0.1:{port}"


class CoreApiAlignmentTests(unittest.TestCase):
    def test_legacy_option_surface_is_removed(self):
        self.assertFalse(hasattr(zlink.Socket, "setsockopt"))
        self.assertFalse(hasattr(zlink.Socket, "getsockopt"))
        self.assertFalse(hasattr(zlink.Socket, "set_option"))
        self.assertFalse(hasattr(zlink.Socket, "get_option"))
        self.assertFalse(hasattr(zlink, "lib"))
        self.assertFalse(hasattr(zlink, "ctypes"))
        self.assertFalse(hasattr(zlink, "ReceivedMessage"))
        self.assertFalse(hasattr(zlink.RouterSocket, "set_router_option"))
        self.assertFalse(hasattr(zlink.RouterSocket, "get_router_option"))
        self.assertFalse(hasattr(zlink.PubSocket, "set_pub_option"))
        self.assertFalse(hasattr(zlink.PubSocket, "get_pub_option"))
        self.assertFalse(hasattr(zlink.SubSocket, "set_sub_option"))
        self.assertFalse(hasattr(zlink.SubSocket, "get_sub_option"))
        self.assertFalse(hasattr(zlink.StreamSocket, "set_stream_option"))
        self.assertFalse(hasattr(zlink.StreamSocket, "get_stream_option"))
        self.assertFalse(hasattr(zlink, "SendFlag"))
        self.assertFalse(hasattr(zlink, "ReceiveFlag"))
        self.assertFalse(hasattr(zlink, "StreamDispatchMode"))

    def test_message_copy_from_copies_input(self):
        source = bytearray(b"alpha")

        with zlink.Message.copy_from(source) as message:
            source[0] = ord("o")
            self.assertEqual(message.to_bytes(), b"alpha")

    def test_message_wrap_buffer_borrows_input(self):
        source = bytearray(b"alpha")

        with zlink.Message.wrap_buffer(source) as message:
            source[0] = ord("o")
            self.assertEqual(message.to_bytes(), b"olpha")

    def test_recv_returns_received_domain_type(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.PairSocket(ctx) as sender:
                with zlink.PairSocket(ctx) as receiver:
                    endpoint = "inproc://py-canonical-recv"
                    sender.bind(endpoint)
                    receiver.connect(endpoint)

                    sender.send(bytearray(b"payload"))
                    with receiver.recv() as received:
                        self.assertIsInstance(received, zlink.Received)
                        self.assertIsNone(received.routing_id)
                        self.assertEqual(received.to_bytes_list(), [b"payload"])
                        self.assertEqual(len(received.parts), 1)

    def test_recv_keeps_other_parts_alive(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.PairSocket(ctx) as sender:
                with zlink.PairSocket(ctx) as receiver:
                    endpoint = "inproc://py-canonical-multipart"
                    sender.bind(endpoint)
                    receiver.connect(endpoint)

                    sender.send([b"one", b"two"])
                    with receiver.recv() as received:
                        self.assertEqual(len(received), 2)
                        first, second = received.parts
                        first.close()
                        self.assertEqual(second.to_bytes(), b"two")
                        with self.assertRaises(RuntimeError):
                            first.to_bytes()

    def test_try_recv_returns_none_when_no_message(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.PairSocket(ctx) as sock:
                self.assertIsNone(sock.try_recv())

    def test_try_send_returns_sent_when_peer_ready(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.PairSocket(ctx) as sender:
                with zlink.PairSocket(ctx) as receiver:
                    endpoint = "inproc://py-try-send"
                    sender.bind(endpoint)
                    receiver.connect(endpoint)
                    self.assertEqual(sender.try_send(b"payload"), zlink.SendResult.SENT)
                    with receiver.recv() as received:
                        self.assertEqual(received.to_bytes_list(), [b"payload"])

    def test_try_send_reports_backpressured_without_peer(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.PairSocket(ctx) as sender:
                endpoint = "inproc://py-try-send-not-ready"
                sender.bind(endpoint)
                self.assertEqual(
                    sender.try_send(b"payload"),
                    zlink.SendResult.BACKPRESSURED,
                )

    def test_try_send_raises_zlink_error_on_general_error(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        sender = zlink.PairSocket(ctx)
        sender.close()
        with self.assertRaises(zlink.ZlinkError):
            sender.try_send(b"payload")

    def test_subscriber_recv_returns_subscribed_domain_type(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.PubSocket(ctx) as publisher:
                with zlink.SubSocket(ctx) as subscriber:
                    endpoint = "inproc://py-canonical-sub"
                    publisher.bind(endpoint)
                    subscriber.connect(endpoint)
                    subscriber.set_subscription(b"topic")

                    publisher.publish(b"topic", [b"payload"])
                    with subscriber.recv() as received:
                        self.assertIsInstance(received, zlink.Subscribed)
                        self.assertEqual(received.topic, b"topic")
                        self.assertIsNone(received.routing_id)
                        self.assertEqual(received.to_bytes_list(), [b"payload"])

    def test_try_subscriber_recv_returns_none_when_no_message(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.SubSocket(ctx) as subscriber:
                self.assertIsNone(subscriber.try_recv())

    def test_try_publish_raises_zlink_error_on_general_error(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        publisher = zlink.PubSocket(ctx)
        publisher.close()
        with self.assertRaises(zlink.ZlinkError):
            publisher.try_publish(b"topic", b"payload")

    def test_blocking_send_failure_surfaces_exception(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        sender = zlink.PairSocket(ctx)
        sender.close()
        with self.assertRaises(zlink.ZlinkError):
            sender.send(b"payload")

    def test_blocking_publish_failure_surfaces_exception(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        publisher = zlink.PubSocket(ctx)
        publisher.close()
        with self.assertRaises(zlink.ZlinkError):
            publisher.publish(b"topic", b"payload")

    def test_try_publish_returns_explicit_sent_outcome(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.PubSocket(ctx) as publisher:
                with zlink.SubSocket(ctx) as subscriber:
                    endpoint = "inproc://py-try-publish"
                    publisher.bind(endpoint)
                    subscriber.connect(endpoint)
                    subscriber.set_subscription(b"topic")
                    self.assertEqual(
                        publisher.try_publish(b"topic", [b"payload"]),
                        zlink.SendResult.SENT,
                    )
                    with subscriber.recv() as received:
                        self.assertEqual(received.to_bytes_list(), [b"payload"])

    def test_option_family_helper_and_poller_use_canonical_paths(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.XPubSocket(ctx) as sender:
                with zlink.XSubSocket(ctx) as receiver:
                    endpoint = "inproc://py-canonical-poller"
                    sender.publisher_options.verbose = True
                    sender.bind(endpoint)
                    receiver.connect(endpoint)
                    receiver.set_subscription(b"topic")

                    with zlink.Poller() as poller:
                        poller.add_socket(sender, zlink.PollEvent.POLLIN, tag="xpub")
                        events = poller.poll(1000)
                        self.assertEqual(len(events), 1)
                        self.assertIs(events[0]["socket"], sender)
                        self.assertEqual(events[0]["tag"], "xpub")

    def test_monitor_snapshot_uses_socket_monitor_handle(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.PairSocket(ctx) as sock:
                with sock.open_monitor(zlink.MonitorEvent.ALL) as monitor:
                    snapshot = monitor.snapshot()
                    self.assertIsInstance(snapshot, zlink.MonitorSnapshot)
                    self.assertIsInstance(snapshot.source_kind, int)
                    self.assertIsInstance(snapshot.state_flags, int)

    def test_monitor_try_recv_returns_none_when_no_event_ready(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.PairSocket(ctx) as sock:
                with sock.open_monitor(zlink.MonitorEvent.ALL) as monitor:
                    self.assertIsNone(monitor.try_recv())

    def test_monitor_recv_surfaces_socket_state_event(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        endpoint = _tcp_endpoint()
        with ctx:
            with zlink.PairSocket(ctx) as server:
                with server.open_monitor(zlink.MonitorEvent.ALL) as monitor:
                    server.bind(endpoint)
                    event = monitor.recv()
                    self.assertIsInstance(event, zlink.SocketMonitorEvent)
                    self.assertNotEqual(event.event, 0)

    def test_registry_and_discovery_use_canonical_service_contract(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.Registry(ctx) as registry:
                registry.set_id(7)
                registry.set_heartbeat(1000, 3000)
                registry.set_broadcast_interval(1500)
                registry.bind("inproc://py-registry-pub", "inproc://py-registry-router")

            with zlink.Discovery(
                ctx, zlink.ServiceType.SOCKET, "orders"
            ) as discovery:
                discovery.set_value(11)
                self.assertEqual(discovery.get_value(), 11)
                discovery.set_metadata(b"meta")
                self.assertEqual(discovery.get_metadata(), b"meta")

    def test_spot_callback_mode_blocks_pollin_poller_registration(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.SpotNode(ctx) as node:
                with zlink.Spot(node) as spot:
                    observed = []

                    def on_message(message):
                        observed.append(message.topic)
                        message.close()

                    spot.set_handler(on_message)
                    with zlink.Poller() as poller:
                        with self.assertRaises(zlink.ZlinkError):
                            poller.add_socket(spot, zlink.PollEvent.POLLIN)

    def test_spot_recv_returns_subscribed_domain_type(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.SpotNode(ctx) as node:
                with zlink.Spot(node) as spot:
                    with spot.open_monitor(
                        zlink.ServiceMonitorMask.SPOT_FILTER_APPLIED
                    ) as monitor:
                        spot.set_send_ready_handler(lambda _: None)
                        spot.set_subscription(b"room:lobby")
                        while True:
                            event = monitor.recv()
                            if (
                                event.event_type
                                == zlink.ServiceMonitorMask.SPOT_FILTER_APPLIED
                            ):
                                break

                        spot.publish(b"room:lobby", [b"hello"])
                        with spot.recv() as received:
                            self.assertIsInstance(received, zlink.Subscribed)
                            self.assertEqual(received.topic, b"room:lobby")
                            self.assertEqual(received.to_bytes_list(), [b"hello"])
                            self.assertIsNotNone(received.routing_id)

    def test_spot_try_recv_returns_none_when_no_message(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.SpotNode(ctx) as node:
                with zlink.Spot(node) as spot:
                    self.assertIsNone(spot.try_recv())

    def test_spot_callback_receives_subscribed_domain_type(self):
        result = subprocess.run(
            [sys.executable, str(EXAMPLES_DIR / "spot_callback.py")],
            cwd=str(ROOT),
            capture_output=True,
            text=True,
            timeout=15,
            env={
                **os.environ,
                **{
                    "PYTHONPATH": str(ROOT / "src"),
                },
            },
        )
        if result.returncode != 0:
            self.fail(result.stdout + result.stderr)
        self.assertIn("topic.unified.cb.", result.stdout)
        self.assertIn("[b'hello']", result.stdout)

    def test_recv_handler_receives_received_and_blocks_direct_recv_model(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.PairSocket(ctx) as sender:
                with zlink.PairSocket(ctx) as receiver:
                    endpoint = _tcp_endpoint()
                    observed = {}
                    ready = threading.Event()

                    def on_message(received):
                        observed["routing_id"] = received.routing_id
                        observed["parts"] = received.to_bytes_list()
                        ready.set()

                    sender.bind(endpoint)
                    receiver.connect(endpoint)
                    receiver.on_receive(on_message)

                    sender.send(b"callback")
                    self.assertTrue(ready.wait(3.0))
                    self.assertIsInstance(observed["routing_id"], (bytes, type(None)))
                    self.assertEqual(observed["parts"], [b"callback"])

                    with self.assertRaises(zlink.ZlinkError):
                        receiver.recv()
                    with zlink.Poller() as poller:
                        with self.assertRaises(zlink.ZlinkError):
                            poller.add_socket(receiver, zlink.PollEvent.POLLIN)

    def test_subscribe_handler_receives_subscribed_and_blocks_direct_recv_model(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.PubSocket(ctx) as publisher:
                with zlink.SubSocket(ctx) as subscriber:
                    endpoint = "inproc://py-callback-sub"
                    observed = {}
                    ready = threading.Event()

                    def on_message(received):
                        observed["topic"] = received.topic
                        observed["parts"] = received.to_bytes_list()
                        ready.set()

                    publisher.bind(endpoint)
                    subscriber.connect(endpoint)
                    subscriber.set_subscription(b"topic")
                    subscriber.on_topic_message(on_message)

                    publisher.publish(b"topic", b"payload")
                    self.assertTrue(ready.wait(3.0))
                    self.assertEqual(observed["topic"], b"topic")
                    self.assertEqual(observed["parts"], [b"payload"])

                    with self.assertRaises(zlink.ZlinkError):
                        subscriber.recv()
                    with zlink.Poller() as poller:
                        with self.assertRaises(zlink.ZlinkError):
                            poller.add_socket(subscriber, zlink.PollEvent.POLLIN)

    def test_send_ready_handler_blocks_pollout_poller_registration(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.PairSocket(ctx) as sender:
                with zlink.PairSocket(ctx) as receiver:
                    endpoint = "inproc://py-send-ready"
                    sender.bind(endpoint)
                    receiver.connect(endpoint)
                    sender.on_send_ready(lambda sock: None)

                    with zlink.Poller() as poller:
                        with self.assertRaises(zlink.ZlinkError):
                            poller.add_socket(sender, zlink.PollEvent.POLLOUT)

    def test_concrete_socket_classes_are_exported_and_dispatched(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.PairSocket(ctx) as direct:
                self.assertIsInstance(direct, zlink.Socket)
                self.assertIs(type(direct), zlink.PairSocket)

            with zlink.Socket(ctx, zlink.SocketType.PAIR) as compat:
                self.assertIsInstance(compat, zlink.Socket)
                self.assertIs(type(compat), zlink.PairSocket)

            with zlink.Socket(ctx, zlink.SocketType.XPUB) as compat_xpub:
                self.assertIs(type(compat_xpub), zlink.XPubSocket)

    def test_surface_restrictions_match_socket_role(self):
        self.assertFalse(hasattr(zlink.PairSocket, "set_routing_id"))
        self.assertFalse(hasattr(zlink.PairSocket, "publish"))
        self.assertFalse(hasattr(zlink.PubSocket, "recv_message"))
        self.assertFalse(hasattr(zlink.PubSocket, "set_routing_id"))
        self.assertFalse(hasattr(zlink.PubSocket, "recv"))
        self.assertFalse(hasattr(zlink.SubSocket, "set_routing_id"))
        self.assertFalse(hasattr(zlink.SubSocket, "subscription_event"))
        self.assertFalse(hasattr(zlink.SubSocket, "subscribe"))
        self.assertFalse(hasattr(zlink.SubSocket, "unsubscribe"))
        self.assertTrue(hasattr(zlink.SubSocket, "recv"))
        self.assertTrue(hasattr(zlink.SubSocket, "set_subscription"))
        self.assertTrue(hasattr(zlink.DealerSocket, "set_routing_id"))
        self.assertTrue(hasattr(zlink.RouterSocket, "send_to"))
        self.assertTrue(hasattr(zlink.RouterSocket, "router_options"))
        self.assertTrue(hasattr(zlink.XPubSocket, "recv_subscription_event"))

    def test_typed_option_surface_uses_capability_objects(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.PairSocket(ctx) as pair:
                pair.options.immediate = True
                self.assertTrue(pair.options.immediate)

            with zlink.XPubSocket(ctx) as xpub:
                self.assertTrue(hasattr(xpub, "publisher_options"))
                xpub.publisher_options.verbose = True
            with zlink.RouterSocket(ctx) as router:
                self.assertTrue(hasattr(router, "router_options"))
                router.router_options.mandatory = False
            with zlink.PairSocket(ctx) as pair:
                self.assertFalse(hasattr(pair, "publisher_options"))
                self.assertFalse(hasattr(pair, "router_options"))

    def test_deprecated_callback_aliases_warn_and_forward(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.PairSocket(ctx) as pair:
                with warnings.catch_warnings(record=True) as caught:
                    warnings.simplefilter("always", DeprecationWarning)
                    pair.set_recv_handler(lambda received: None)
                self.assertEqual(len(caught), 1)
                self.assertIn("on_receive()", str(caught[0].message))

            with zlink.SubSocket(ctx) as sub:
                with warnings.catch_warnings(record=True) as caught:
                    warnings.simplefilter("always", DeprecationWarning)
                    sub.set_subscribe_handler(lambda received: None)
                self.assertEqual(len(caught), 1)
                self.assertIn("on_topic_message()", str(caught[0].message))

            with zlink.PairSocket(ctx) as pair:
                with warnings.catch_warnings(record=True) as caught:
                    warnings.simplefilter("always", DeprecationWarning)
                    pair.set_send_ready_handler(lambda sock: None)
                self.assertEqual(len(caught), 1)
                self.assertIn("on_send_ready()", str(caught[0].message))

    def test_xpub_subscription_event_remains_on_xpub_surface(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.XPubSocket(ctx) as xpub:
                with zlink.XSubSocket(ctx) as xsub:
                    endpoint = "inproc://py-xpub-subscription-event"
                    xpub.publisher_options.verbose = True
                    xpub.bind(endpoint)
                    xsub.connect(endpoint)
                    xsub.set_subscription(b"topic")

                    event = xpub.recv_subscription_event()
                    self.assertTrue(event.subscribed)
                    self.assertEqual(event.topic, b"topic")

    def test_xpub_try_subscription_event_returns_none_when_no_event(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.XPubSocket(ctx) as xpub:
                self.assertIsNone(xpub.try_recv_subscription_event())

    def test_routing_id_accepts_max_length_and_rejects_overflow(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        max_routing_id = b"r" * 255
        too_long_routing_id = b"r" * 256

        with ctx:
            with zlink.DealerSocket(ctx) as dealer:
                dealer.set_routing_id(max_routing_id)
                self.assertEqual(dealer.get_routing_id(), max_routing_id)
                with self.assertRaises(ValueError):
                    dealer.set_routing_id(too_long_routing_id)

            with zlink.RouterSocket(ctx) as router:
                router.router_options.connect_routing_id = max_routing_id
                with self.assertRaises(ValueError):
                    router.router_options.connect_routing_id = too_long_routing_id

            with zlink.SpotNode(ctx) as node:
                node.set_routing_id(max_routing_id)
                self.assertEqual(node.get_routing_id(), max_routing_id)
                with self.assertRaises(ValueError):
                    node.set_routing_id(too_long_routing_id)

    def test_router_socket_connect_routing_id_uses_typed_option_surface(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        endpoint = _tcp_endpoint()
        with ctx:
            with zlink.RouterSocket(ctx) as server:
                with zlink.RouterSocket(ctx) as client:
                    server.set_routing_id(b"SERVER")
                    client.set_routing_id(b"CLIENT")
                    client.router_options.connect_routing_id = b"SERVER"
                    server.bind(endpoint)
                    client.connect(endpoint)

                    client.send_to(b"SERVER", b"ping")
                    with server.recv() as request:
                        self.assertEqual(request.routing_id, b"CLIENT")
                        self.assertEqual(request.to_bytes_list(), [b"ping"])
                        server.send_to(request.routing_id, b"pong")

                    with client.recv() as reply:
                        self.assertEqual(reply.routing_id, b"SERVER")
                        self.assertEqual(reply.to_bytes_list(), [b"pong"])

    def test_spot_node_wrap_handle_and_snapshots_follow_core_contract(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        endpoint = _tcp_endpoint()
        with ctx:
            with zlink.SpotNode(ctx) as node:
                node.set_routing_id(b"NODE")
                node.bind(endpoint)
                with node.wrap_handle() as spot:
                    status = node.status_snapshot()
                    self.assertIsInstance(status, zlink.SpotNodeStatus)
                    self.assertEqual(status.node_routing_id, b"NODE")
                    self.assertEqual(status.local_endpoint, endpoint)
                    self.assertIsInstance(status.state, zlink.SpotNodeState)
                    self.assertEqual(node.peers_snapshot(), [])
                    self.assertEqual(
                        node.peers_query(zlink.SpotNodePeerFilter()), []
                    )
                    self.assertEqual(node.subjects_snapshot(), [])
                    self.assertIsInstance(spot, zlink.Spot)

    def test_stream_recv_round_trip_with_raw_tcp_peer(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
        listener.close()
        endpoint = f"tcp://127.0.0.1:{port}"

        with ctx:
            with zlink.StreamSocket(ctx) as server:
                server.bind(endpoint)
                with socket.create_connection(("127.0.0.1", port), timeout=3.0) as client:
                    client.sendall(b"stream-recv")
                    with server.recv() as received:
                        self.assertEqual(received.to_bytes_list(), [b"stream-recv"])
                        self.assertIsNotNone(received.routing_id)
                        server.send_to(received.routing_id, b"stream-reply")
                    reply = client.recv(64)
                    self.assertEqual(reply, b"stream-reply")

    def test_stream_callback_round_trip_with_raw_tcp_peer(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
        listener.close()
        endpoint = f"tcp://127.0.0.1:{port}"
        ready = threading.Event()
        observed = {}

        with ctx:
            with zlink.StreamSocket(ctx) as server:
                def on_message(received):
                    observed["routing_id"] = received.routing_id
                    observed["parts"] = received.to_bytes_list()
                    ready.set()

                server.on_receive(on_message)
                server.bind(endpoint)
                with socket.create_connection(("127.0.0.1", port), timeout=3.0) as client:
                    client.sendall(b"stream-callback")
                    self.assertTrue(ready.wait(3.0))
                    self.assertEqual(observed["parts"], [b"stream-callback"])
                    server.send_to(observed["routing_id"], b"stream-callback-reply")
                    reply = client.recv(64)
                    self.assertEqual(reply, b"stream-callback-reply")

    def test_example_runner_executes_all_examples(self):
        result = subprocess.run(
            [sys.executable, str(EXAMPLES_DIR / "run_all_examples.py")],
            cwd=str(ROOT),
            capture_output=True,
            text=True,
            timeout=90,
            env={
                **os.environ,
                **{
                    "PYTHONPATH": str(ROOT / "src"),
                },
            },
        )
        if result.returncode != 0:
            self.fail(result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
