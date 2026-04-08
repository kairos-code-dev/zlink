import socket
import os
import signal
import subprocess
import sys
import threading
import time
import unittest
from pathlib import Path

import zlink
from zlink import _socket_base


ROOT = Path(__file__).resolve().parents[1]
SAMPLES_DIR = ROOT / "samples"


def _tcp_endpoint():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return f"tcp://127.0.0.1:{port}"


def _run_python_script(script_path, *, timeout, env):
    proc = subprocess.Popen(
        [sys.executable, str(script_path)],
        cwd=str(ROOT),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        env=env,
        start_new_session=True,
    )
    try:
        stdout, stderr = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        os.killpg(proc.pid, signal.SIGTERM)
        try:
            stdout, stderr = proc.communicate(timeout=2)
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
            stdout, stderr = proc.communicate()
    return subprocess.CompletedProcess(proc.args, proc.returncode, stdout, stderr)


class CoreApiAlignmentTests(unittest.TestCase):
    def test_legacy_option_surface_is_removed(self):
        self.assertFalse(hasattr(zlink, "Socket"))
        self.assertFalse(hasattr(zlink, "lib"))
        self.assertFalse(hasattr(zlink, "ctypes"))
        self.assertFalse(hasattr(zlink, "ReceivedMessage"))
        self.assertFalse(hasattr(zlink.Context, "set"))
        self.assertFalse(hasattr(zlink.Context, "get"))
        self.assertFalse(hasattr(zlink.Message, "gets"))
        self.assertFalse(hasattr(zlink.Message, "refcnt"))
        self.assertFalse(hasattr(zlink.Message, "view"))
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

    def test_internal_socket_base_module_uses_internal_only_names(self):
        self.assertFalse(hasattr(_socket_base, "Socket"))
        self.assertFalse(hasattr(_socket_base, "BindSocket"))
        self.assertFalse(hasattr(_socket_base, "ConnectSocket"))
        self.assertFalse(hasattr(_socket_base, "EndpointSocket"))
        self.assertFalse(hasattr(_socket_base, "RoutingIdSocket"))
        self.assertFalse(hasattr(_socket_base, "DealerOptionSocket"))
        self.assertFalse(hasattr(_socket_base, "RouterOptionSocket"))
        self.assertFalse(hasattr(_socket_base, "StreamOptionSocket"))
        self.assertFalse(hasattr(_socket_base, "PublisherOptionSocket"))
        self.assertFalse(hasattr(_socket_base, "SubscriberOptionSocket"))
        self.assertFalse(hasattr(_socket_base, "MessageSocket"))
        self.assertFalse(hasattr(_socket_base, "RoutedMessageSocket"))
        self.assertFalse(hasattr(_socket_base, "PublisherSocket"))
        self.assertFalse(hasattr(_socket_base, "SubscriberSocket"))
        self.assertFalse(hasattr(zlink.Received, "messages"))
        self.assertFalse(hasattr(zlink.TopicMessage, "messages"))

    def test_message_copy_from_copies_input(self):
        source = bytearray(b"alpha")

        with zlink.Message.copy_from(source) as message:
            source[0] = ord("o")
            self.assertEqual(message.to_bytes(), b"alpha")

    def test_message_wrap_buffer_borrows_input(self):
        source = bytearray(b"alpha")

        with zlink.Message.wrap_buffer(source) as message:
            self.assertIsInstance(message.data, memoryview)
            self.assertEqual(message.data.tobytes(), b"alpha")
            source[0] = ord("o")
            self.assertEqual(message.data.tobytes(), b"olpha")
            self.assertEqual(message.to_bytes(), b"olpha")

    def test_message_diagnostics_use_canonical_surface(self):
        with zlink.Message.copy_from(b"alpha") as message:
            self.assertTrue(hasattr(message, "getProperty"))
            self.assertTrue(hasattr(message, "refCount"))
            self.assertIsNone(message.getProperty("Socket-Type"))
            self.assertGreaterEqual(message.refCount(), 1)

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

    def test_subscriber_subscribe_returns_subscribed_domain_type(self):
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
                    with subscriber.subscribe() as received:
                        self.assertIsInstance(received, zlink.Subscribed)
                        self.assertEqual(received.topic, b"topic")
                        self.assertIsNone(received.routing_id)
                        self.assertEqual(received.to_bytes_list(), [b"payload"])

    def test_try_subscriber_subscribe_returns_none_when_no_message(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.SubSocket(ctx) as subscriber:
                self.assertIsNone(subscriber.try_subscribe())

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
                    with subscriber.subscribe() as received:
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
                with sock.monitor_open() as monitor:
                    snapshot = monitor.snapshot()
                    self.assertIsInstance(snapshot, zlink.MonitorSnapshot)
                    self.assertIsInstance(snapshot.source_kind, int)
                    self.assertIsInstance(snapshot.state_flags, int)

    def test_legacy_open_monitor_alias_is_not_public(self):
        self.assertFalse(hasattr(zlink.PairSocket, "open_monitor"))
        self.assertFalse(hasattr(zlink.Discovery, "open_monitor"))

    def test_monitor_try_recv_returns_none_when_no_event_ready(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.PairSocket(ctx) as sock:
                with sock.monitor_open() as monitor:
                    self.assertIsNone(monitor.try_recv())

    def test_monitor_recv_surfaces_socket_state_event(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        endpoint = _tcp_endpoint()
        with ctx:
            with zlink.PairSocket(ctx) as server:
                with server.monitor_open() as monitor:
                    server.bind(endpoint)
                    event = monitor.recv()
                    self.assertIsInstance(event, zlink.SocketMonitorEvent)
                    self.assertNotEqual(event.event, 0)

    def test_socket_monitor_on_event_uses_python_dispatch_thread(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        endpoint = _tcp_endpoint()
        with ctx:
            with zlink.PairSocket(ctx) as server:
                observed = {}
                ready = threading.Event()

                def on_event(event):
                    observed["thread_name"] = threading.current_thread().name
                    observed["event"] = event
                    ready.set()

                with server.monitor_open(zlink.MonitorEvent.LISTENING) as monitor:
                    monitor.on_event(on_event)
                    server.bind(endpoint)
                    self.assertTrue(ready.wait(5.0))
                    self.assertIsInstance(observed["event"], zlink.SocketMonitorEvent)
                    self.assertTrue(
                        observed["thread_name"].startswith("zlink-monitor-event"),
                        observed["thread_name"],
                    )

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
                self.assertTrue(hasattr(registry, "set_tls_server"))
                self.assertTrue(hasattr(registry, "set_tls_client"))

            with zlink.Discovery(
                ctx, zlink.ServiceType.SOCKET, "orders"
            ) as discovery:
                discovery.set_value(11)
                self.assertEqual(discovery.get_value(), 11)
                discovery.set_metadata(b"meta")
                self.assertEqual(discovery.get_metadata(), b"meta")
                self.assertTrue(hasattr(discovery, "set_tls_client"))
                with discovery.monitor_open() as monitor:
                    self.assertIsNone(monitor.try_recv())

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

                    spot.on_subscribe(on_message)
                    with zlink.Poller() as poller:
                        with self.assertRaises(zlink.ZlinkError):
                            poller.add_socket(spot, zlink.PollEvent.POLLIN)

    def test_spot_subscribe_returns_subscribed_domain_type(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.SpotNode(ctx) as server_node:
                with zlink.SpotNode(ctx) as client_node:
                    with zlink.Spot(server_node) as server_spot:
                        with zlink.Spot(client_node) as client_spot:
                            endpoint = _tcp_endpoint()
                            server_node.bind(endpoint)
                            client_node.connect_peer(endpoint)
                            client_spot.set_subscription(b"room:lobby")
                            deadline = time.monotonic() + 5.0
                            while time.monotonic() < deadline:
                                server_spot.publish(b"room:lobby", [b"hello"])
                                received = client_spot.try_subscribe()
                                if received is None:
                                    time.sleep(0.01)
                                    continue
                                with received:
                                    self.assertIsInstance(received, zlink.Subscribed)
                                    self.assertEqual(received.topic, b"room:lobby")
                                self.assertEqual(received.to_bytes_list(), [b"hello"])
                                self.assertIsNotNone(received.routing_id)
                                break
                            else:
                                self.fail("spot subscribe did not receive direct peer payload")

    def test_spot_callback_uses_python_dispatch_thread(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.SpotNode(ctx) as server_node:
                with zlink.SpotNode(ctx) as client_node:
                    with zlink.Spot(server_node) as server_spot:
                        with zlink.Spot(client_node) as client_spot:
                            endpoint = _tcp_endpoint()
                            server_node.bind(endpoint)
                            client_node.connect_peer(endpoint)
                            client_spot.set_subscription(b"room:lobby")
                            observed = {}
                            ready = threading.Event()

                            def on_message(message):
                                observed["thread_name"] = threading.current_thread().name
                                observed["topic"] = message.topic
                                observed["parts"] = message.to_bytes_list()
                                ready.set()
                                message.close()

                            client_spot.on_subscribe(on_message)
                            deadline = time.monotonic() + 5.0
                            while time.monotonic() < deadline and not ready.is_set():
                                server_spot.publish(b"room:lobby", [b"hello"])
                                ready.wait(0.05)
                            self.assertTrue(ready.is_set())
                            self.assertTrue(
                                observed["thread_name"].startswith("zlink-spot-sub"),
                                observed["thread_name"],
                            )
                            self.assertEqual(observed["topic"], b"room:lobby")
                            self.assertEqual(observed["parts"], [b"hello"])

    def test_spot_try_subscribe_returns_none_when_no_message(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.SpotNode(ctx) as node:
                with zlink.Spot(node) as spot:
                    self.assertIsNone(spot.try_subscribe())

    def test_spot_callback_receives_subscribed_domain_type(self):
        result = _run_python_script(
            SAMPLES_DIR / "spot_callback_sample.py",
            timeout=30,
            env={
                **os.environ,
                **{
                    "PYTHONPATH": str(ROOT / "src") + os.pathsep + str(SAMPLES_DIR),
                },
            },
        )
        if result.returncode != 0:
            self.fail(result.stdout + result.stderr)
        self.assertIn('[spot/callback] publish: "room:lobby/hello-spot"', result.stdout)

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
                    self.assertIsInstance(
                        observed["routing_id"],
                        (zlink.RoutingId, type(None)),
                    )
                    self.assertEqual(observed["parts"], [b"callback"])

                    with self.assertRaises(zlink.ZlinkError):
                        receiver.recv()
                    with zlink.Poller() as poller:
                        with self.assertRaises(zlink.ZlinkError):
                            poller.add_socket(receiver, zlink.PollEvent.POLLIN)

    def test_on_subscribe_receives_subscribed_and_blocks_direct_subscribe_model(self):
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
                        observed["thread_name"] = threading.current_thread().name
                        observed["topic"] = received.topic
                        observed["parts"] = received.to_bytes_list()
                        ready.set()

                    publisher.bind(endpoint)
                    subscriber.connect(endpoint)
                    subscriber.set_subscription(b"topic")
                    subscriber.on_subscribe(on_message)

                    publisher.publish(b"topic", b"payload")
                    self.assertTrue(ready.wait(3.0))
                    self.assertTrue(
                        observed["thread_name"].startswith("zlink-socket-subscribe"),
                        observed["thread_name"],
                    )
                    self.assertEqual(observed["topic"], b"topic")
                    self.assertEqual(observed["parts"], [b"payload"])

                    with self.assertRaises(zlink.ZlinkError):
                        subscriber.subscribe()
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
                    payload = b"priming" * 8192

                    sender.options.send_high_water_mark = 1
                    receiver.options.receive_high_water_mark = 1
                    sender.bind(endpoint)
                    receiver.connect(endpoint)

                    primed = 0
                    backpressured = False
                    for _ in range(8192):
                        result = sender.try_send(payload)
                        if result == zlink.SendResult.BACKPRESSURED:
                            backpressured = True
                            break
                        self.assertEqual(result, zlink.SendResult.SENT)
                        primed += 1
                    self.assertGreater(primed, 0)
                    self.assertTrue(backpressured, "sender never became backpressured")

                    sender.on_send_ready(lambda sock: None)

                    with zlink.Poller() as poller:
                        with self.assertRaises(zlink.ZlinkError):
                            poller.add_socket(sender, zlink.PollEvent.POLLOUT)

    def test_concrete_socket_classes_are_exported_without_generic_socket_factory(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.PairSocket(ctx) as direct:
                self.assertIs(type(direct), zlink.PairSocket)
            with zlink.XPubSocket(ctx) as xpub:
                self.assertIs(type(xpub), zlink.XPubSocket)

    def test_surface_restrictions_match_socket_role(self):
        self.assertFalse(hasattr(zlink.PairSocket, "set_routing_id"))
        self.assertFalse(hasattr(zlink.PairSocket, "publish"))
        self.assertFalse(hasattr(zlink.PubSocket, "recv_message"))
        self.assertFalse(hasattr(zlink.PubSocket, "set_routing_id"))
        self.assertFalse(hasattr(zlink.PubSocket, "recv"))
        self.assertFalse(hasattr(zlink.SubSocket, "set_routing_id"))
        self.assertFalse(hasattr(zlink.SubSocket, "subscription_event"))
        self.assertFalse(hasattr(zlink.SubSocket, "recv"))
        self.assertFalse(hasattr(zlink.SubSocket, "try_recv"))
        self.assertFalse(hasattr(zlink.SubSocket, "on_topic_message"))
        self.assertFalse(hasattr(zlink.SubSocket, "set_subscribe_handler"))
        self.assertTrue(hasattr(zlink.SubSocket, "subscribe"))
        self.assertTrue(hasattr(zlink.SubSocket, "try_subscribe"))
        self.assertTrue(hasattr(zlink.SubSocket, "on_subscribe"))
        self.assertTrue(hasattr(zlink.SubSocket, "set_subscription"))
        self.assertTrue(hasattr(zlink.DealerSocket, "set_routing_id"))
        self.assertFalse(hasattr(zlink.StreamSocket, "connect"))
        self.assertTrue(hasattr(zlink.RouterSocket, "send"))
        self.assertTrue(hasattr(zlink.RouterSocket, "router_options"))
        self.assertTrue(hasattr(zlink.XPubSocket, "receive_subscription_event"))
        self.assertFalse(hasattr(zlink.Spot, "recv"))
        self.assertFalse(hasattr(zlink.Spot, "try_recv"))
        self.assertFalse(hasattr(zlink.Spot, "set_handler"))
        self.assertFalse(hasattr(zlink.Spot, "set_send_ready_handler"))
        self.assertTrue(hasattr(zlink.Spot, "subscribe"))
        self.assertTrue(hasattr(zlink.Spot, "try_subscribe"))

    def test_typed_option_surface_uses_capability_objects(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            self.assertTrue(hasattr(ctx, "options"))
            ctx.options.ioThreads = 1
            self.assertEqual(ctx.options.ioThreads, 1)
            ctx.options.maxSockets = 1024
            self.assertEqual(ctx.options.maxSockets, 1024)
            self.assertFalse(hasattr(zlink.Context, "set"))
            self.assertFalse(hasattr(zlink.Context, "get"))

            with zlink.PairSocket(ctx) as pair:
                pair.options.immediate = True
                self.assertTrue(pair.options.immediate)

            with zlink.XPubSocket(ctx) as xpub:
                self.assertTrue(hasattr(xpub, "publisher_options"))
                xpub.publisher_options.verbose = True
                xpub.publisher_options.manual = False
                self.assertFalse(xpub.publisher_options.manual)
            with zlink.RouterSocket(ctx) as router:
                self.assertTrue(hasattr(router, "router_options"))
                router.router_options.mandatory = False
            with zlink.DealerSocket(ctx) as dealer:
                self.assertTrue(hasattr(dealer, "dealer_options"))
                dealer.dealer_options.probe = True
                self.assertTrue(dealer.dealer_options.probe)
            with zlink.StreamSocket(ctx) as stream:
                self.assertTrue(hasattr(stream, "stream_options"))
                stream.stream_options.notify = False
                self.assertFalse(stream.stream_options.notify)
            with zlink.SubSocket(ctx) as sub:
                self.assertTrue(hasattr(sub, "subscriber_options"))
                self.assertEqual(sub.subscriber_options.topics_count, 0)
            with zlink.PairSocket(ctx) as pair:
                self.assertFalse(hasattr(pair, "publisher_options"))
                self.assertFalse(hasattr(pair, "router_options"))
                self.assertFalse(hasattr(pair, "dealer_options"))
                self.assertFalse(hasattr(pair, "stream_options"))
                self.assertFalse(hasattr(pair, "subscriber_options"))

    def test_legacy_callback_aliases_are_removed(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            self.assertFalse(hasattr(zlink.PairSocket, "set_recv_handler"))
            self.assertFalse(hasattr(zlink.PairSocket, "set_send_ready_handler"))
            self.assertTrue(hasattr(zlink.PairSocket, "on_receive"))
            self.assertTrue(hasattr(zlink.PairSocket, "on_send_ready"))
            with zlink.PairSocket(ctx) as pair:
                self.assertFalse(hasattr(pair, "set_recv_handler"))
                self.assertFalse(hasattr(pair, "set_send_ready_handler"))

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

                    event = xpub.receive_subscription_event()
                    self.assertTrue(event.subscribed)
                    self.assertEqual(event.topic, b"topic")

    def test_xpub_try_subscription_event_returns_none_when_no_event(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.XPubSocket(ctx) as xpub:
                self.assertIsNone(xpub.try_receive_subscription_event())

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
                self.assertEqual(dealer.get_routing_id(), zlink.RoutingId(max_routing_id))
                with self.assertRaises(ValueError):
                    dealer.set_routing_id(too_long_routing_id)

            with zlink.RouterSocket(ctx) as router:
                router.router_options.connect_routing_id = max_routing_id
                with self.assertRaises(ValueError):
                    router.router_options.connect_routing_id = too_long_routing_id

            with zlink.SpotNode(ctx) as node:
                node.set_routing_id(max_routing_id)
                self.assertEqual(node.get_routing_id(), zlink.RoutingId(max_routing_id))
                with self.assertRaises(ValueError):
                    node.set_routing_id(too_long_routing_id)

    def test_int32_and_uint32_boundaries_fail_fast(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with self.assertRaises(OverflowError):
                ctx.options.ioThreads = 1 << 31
            with zlink.PairSocket(ctx) as pair:
                with self.assertRaises(OverflowError):
                    pair.options.linger_ms = 1 << 31
                with self.assertRaises(OverflowError):
                    pair.options.send_timeout_ms = -(1 << 31) - 1

            with zlink.Registry(ctx) as registry:
                with self.assertRaises(OverflowError):
                    registry.set_broadcast_interval(1 << 32)
                with self.assertRaises(OverflowError):
                    registry.set_heartbeat(-1, 1000)

    def test_int32_max_boundary_is_accepted(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.PairSocket(ctx) as pair:
                pair.options.linger_ms = (1 << 31) - 1
                self.assertEqual(pair.options.linger_ms, (1 << 31) - 1)

    def test_c_string_inputs_reject_embedded_nul(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.PubSocket(ctx) as publisher:
                with self.assertRaises(ValueError):
                    publisher.publish(b"bad\0topic", b"payload")
                with self.assertRaises(ValueError):
                    publisher.try_publish(b"bad\0topic", b"payload")

            with zlink.SubSocket(ctx) as subscriber:
                with self.assertRaises(ValueError):
                    subscriber.set_subscription(b"bad\0topic")

            with zlink.SpotNode(ctx) as node:
                with zlink.Spot(node) as spot:
                    with self.assertRaises(ValueError):
                        spot.publish(b"bad\0topic", b"payload")
                    with self.assertRaises(ValueError):
                        spot.try_publish(b"bad\0topic", b"payload")

            with zlink.PairSocket(ctx) as pair:
                with self.assertRaises(ValueError):
                    pair.bind("tcp://127.0.0.1\0:5555")

    def test_fixed_length_service_strings_fail_fast(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        max_length = "a" * 255
        too_long = "a" * 256

        with ctx:
            with zlink.Discovery(ctx, zlink.ServiceType.SOCKET, max_length):
                pass

            with self.assertRaises(ValueError):
                zlink.Discovery(ctx, zlink.ServiceType.SOCKET, too_long)

            with zlink.Registry(ctx) as registry:
                with self.assertRaises(ValueError):
                    registry.bind(too_long, "tcp://127.0.0.1:5555")

            with zlink.PairSocket(ctx) as pair:
                with self.assertRaises(ValueError):
                    pair.bind(too_long)

            with zlink.SpotNode(ctx) as node:
                with self.assertRaises(ValueError):
                    node.bind(too_long)

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

                    self.assertEqual(
                        client.router_options.connect_routing_id,
                        zlink.RoutingId(b"SERVER"),
                    )
                    client.send(b"ping", routing_id=b"SERVER")
                    with server.recv() as request:
                        self.assertEqual(request.routing_id, zlink.RoutingId(b"CLIENT"))
                        self.assertEqual(request.to_bytes_list(), [b"ping"])
                        server.send(b"pong", routing_id=request.routing_id)

                    with client.recv() as reply:
                        self.assertEqual(reply.routing_id, zlink.RoutingId(b"SERVER"))
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
                    self.assertEqual(status.node_routing_id, zlink.RoutingId(b"NODE"))
                    self.assertEqual(status.local_endpoint, endpoint)
                    self.assertIsInstance(status.state, zlink.SpotNodeState)
                    self.assertEqual(node.peers_snapshot(), [])
                    self.assertEqual(
                        node.peers_query(zlink.SpotNodePeerFilter()), []
                    )
                    self.assertEqual(node.subjects_snapshot(), [])
                    self.assertIsInstance(spot, zlink.Spot)

    def test_discovery_monitor_on_event_uses_typed_service_event(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.Registry(ctx) as registry:
                with zlink.Discovery(ctx, zlink.ServiceType.SPOT, "py-monitor") as discovery:
                    with zlink.SpotNode(ctx) as node:
                        observed = {}
                        ready = threading.Event()
                        pub_endpoint = _tcp_endpoint()
                        router_endpoint = _tcp_endpoint()
                        service_endpoint = _tcp_endpoint()

                        def on_event(event):
                            observed["thread_name"] = threading.current_thread().name
                            observed["event"] = event
                            ready.set()

                        registry.bind(pub_endpoint, router_endpoint)
                        discovery.connect_registry(router_endpoint)
                        node.attach_discovery(discovery)

                        with discovery.monitor_open(
                            zlink.ServiceMonitorMask.DISCOVERY_SERVICE_UP
                        ) as monitor:
                            monitor.on_event(on_event)
                            node.bind(service_endpoint)
                            self.assertTrue(ready.wait(5.0))
                            self.assertIsInstance(observed["event"], zlink.ServiceEvent)
                            self.assertEqual(
                                observed["event"].event_type,
                                zlink.ServiceMonitorMask.DISCOVERY_SERVICE_UP,
                            )
                            self.assertTrue(
                                observed["thread_name"].startswith(
                                    "zlink-monitor-event"
                                ),
                                observed["thread_name"],
                            )

    def test_resource_owners_support_async_context_manager_protocol(self):
        import asyncio

        async def _exercise():
            async with zlink.Context() as ctx:
                async with zlink.Poller() as poller:
                    self.assertIsNotNone(poller)
                async with zlink.Message.copy_from(b"payload") as message:
                    self.assertEqual(message.to_bytes(), b"payload")
                async with zlink.PairSocket(ctx) as sock:
                    self.assertIsNotNone(sock)
                async with zlink.Registry(ctx) as registry:
                    self.assertIsNotNone(registry)

        asyncio.run(_exercise())

    def test_attach_discovery_blocks_manual_socket_lifecycle_paths(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        discovery = zlink.Discovery(ctx, zlink.ServiceType.SOCKET, "orders")
        publisher = zlink.PubSocket(ctx)
        try:
            publisher.bind("inproc://py-attach-discovery")
            publisher.attach_discovery(discovery)
            with self.assertRaises(zlink.ZlinkError):
                publisher.unbind("inproc://py-attach-discovery")
            with self.assertRaises(zlink.ZlinkError):
                publisher.connect("tcp://127.0.0.1:5555")
            with self.assertRaises(zlink.ZlinkError):
                publisher.disconnect("tcp://127.0.0.1:5555")
            with self.assertRaises(zlink.ZlinkError):
                publisher.close()
        finally:
            discovery.close()
            ctx.close()

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
                        server.send(b"stream-reply", routing_id=received.routing_id)
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

                server.bind(endpoint)
                server.on_receive(on_message)
                with socket.create_connection(("127.0.0.1", port), timeout=3.0) as client:
                    client.sendall(b"stream-callback")
                    self.assertTrue(ready.wait(3.0))
                    self.assertEqual(observed["parts"], [b"stream-callback"])
                    server.send(
                        b"stream-callback-reply",
                        routing_id=observed["routing_id"],
                    )
                    reply = client.recv(64)
                    self.assertEqual(reply, b"stream-callback-reply")

    def test_exported_domain_types_match_canonical_api(self):
        self.assertIs(zlink.Subscribed.__mro__[1], zlink.TopicMessage)
        self.assertTrue(hasattr(zlink, "RoutingId"))
        self.assertTrue(hasattr(zlink, "TopicMessage"))
        self.assertTrue(hasattr(zlink, "ContextOptions"))
        self.assertTrue(hasattr(zlink, "ServiceEvent"))
        self.assertTrue(hasattr(zlink, "MemberPeerEntry"))
        self.assertTrue(hasattr(zlink, "RegistryStatus"))
        self.assertTrue(hasattr(zlink, "RegistryTopologyEntry"))
        self.assertTrue(hasattr(zlink, "RegistryQueryClient"))
        self.assertTrue(hasattr(zlink, "ServiceRole"))
        self.assertTrue(hasattr(zlink, "RegistryState"))
        self.assertTrue(hasattr(zlink, "TopologySource"))
        self.assertTrue(hasattr(zlink, "TopologyState"))

    def test_example_runner_executes_all_examples(self):
        result = _run_python_script(
            ROOT / "examples" / "run_all_examples.py",
            timeout=180,
            env={
                **os.environ,
                **{
                    "PYTHONPATH": (
                        str(ROOT / "src")
                        + os.pathsep
                        + str(SAMPLES_DIR)
                        + os.pathsep
                        + str(ROOT / "examples")
                    ),
                },
            },
        )
        if result.returncode != 0:
            self.fail(result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
