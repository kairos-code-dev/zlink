import socket
import threading
import unittest
import warnings

import zlink


def _tcp_endpoint(label):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return f"tcp://127.0.0.1:{port}"


class CoreApiAlignmentTests(unittest.TestCase):
    def test_legacy_option_surface_is_removed(self):
        self.assertFalse(hasattr(zlink.Socket, "setsockopt"))
        self.assertFalse(hasattr(zlink.Socket, "getsockopt"))

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

    def test_recv_message_and_recv_into_use_canonical_api(self):
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
                    with receiver.recv_message() as received:
                        self.assertEqual(bytes(received.view()), b"payload")

                    sender.send(b"buffered")
                    buffer = bytearray(32)
                    written = receiver.recv_into(memoryview(buffer))
                    self.assertEqual(written, 8)
                    self.assertEqual(bytes(buffer[:written]), b"buffered")

    def test_recv_multipart_keeps_other_parts_alive(self):
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

                    sender.send_multipart([b"one", b"two"])
                    with receiver.recv_multipart() as received:
                        self.assertEqual(len(received), 2)
                        first, second = received.messages
                        first.close()
                        self.assertEqual(second.to_bytes(), b"two")
                        with self.assertRaises(RuntimeError):
                            first.to_bytes()

    def test_option_family_helper_and_poller_use_canonical_paths(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.XPubSocket(ctx) as sender:
                with zlink.XSubSocket(ctx) as receiver:
                    endpoint = "inproc://py-canonical-poller"
                    sender.set_pub_option(0x3301, (1).to_bytes(4, "little"))
                    sender.bind(endpoint)
                    receiver.connect(endpoint)
                    receiver.subscribe(b"topic")

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
                    self.assertIn("source_kind", snapshot)
                    self.assertIn("state_flags", snapshot)

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
            with zlink.Spot(ctx) as spot:
                observed = []

                def on_message(message):
                    observed.append(message.topic)
                    message.close()

                spot.set_handler(on_message)
                with zlink.Poller() as poller:
                    with self.assertRaises(zlink.ZlinkError):
                        poller.add_socket(spot, zlink.PollEvent.POLLIN)

    def test_recv_handler_receives_message_and_blocks_direct_recv_model(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.PairSocket(ctx) as sender:
                with zlink.PairSocket(ctx) as receiver:
                    endpoint = _tcp_endpoint("py-callback-pair")
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
                        receiver.recv_message()
                    with zlink.Poller() as poller:
                        with self.assertRaises(zlink.ZlinkError):
                            poller.add_socket(receiver, zlink.PollEvent.POLLIN)

    def test_subscribe_handler_receives_topic_and_blocks_direct_recv_model(self):
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
                    subscriber.subscribe(b"topic")
                    subscriber.on_topic_message(on_message)

                    publisher.publish(b"topic", b"payload")
                    self.assertTrue(ready.wait(3.0))
                    self.assertEqual(observed["topic"], b"topic")
                    self.assertEqual(observed["parts"], [b"payload"])

                    with self.assertRaises(zlink.ZlinkError):
                        subscriber.recv_topic_message()
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
        self.assertFalse(hasattr(zlink.PairSocket, "publish"))
        self.assertFalse(hasattr(zlink.PubSocket, "recv_message"))
        self.assertFalse(hasattr(zlink.SubSocket, "subscription_event"))
        self.assertTrue(hasattr(zlink.XPubSocket, "subscription_event"))
        self.assertTrue(hasattr(zlink.SubSocket, "on_topic_message"))
        self.assertTrue(hasattr(zlink.PairSocket, "on_receive"))

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
                    xpub.set_pub_option(0x3301, (1).to_bytes(4, "little"))
                    xpub.bind(endpoint)
                    xsub.connect(endpoint)
                    xsub.subscribe(b"topic")

                    event = xpub.subscription_event()
                    self.assertTrue(event["subscribed"])
                    self.assertEqual(event["topic"], b"topic")


if __name__ == "__main__":
    unittest.main()
