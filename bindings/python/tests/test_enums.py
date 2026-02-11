import unittest

import zlink


class EnumValueTests(unittest.TestCase):
    """Verify enum values match C header #define constants."""

    def test_socket_type_values(self):
        self.assertEqual(int(zlink.SocketType.PAIR), 0)
        self.assertEqual(int(zlink.SocketType.PUB), 1)
        self.assertEqual(int(zlink.SocketType.SUB), 2)
        self.assertEqual(int(zlink.SocketType.DEALER), 5)
        self.assertEqual(int(zlink.SocketType.ROUTER), 6)
        self.assertEqual(int(zlink.SocketType.XPUB), 9)
        self.assertEqual(int(zlink.SocketType.XSUB), 10)
        self.assertEqual(int(zlink.SocketType.STREAM), 11)

    def test_context_option_values(self):
        self.assertEqual(int(zlink.ContextOption.IO_THREADS), 1)
        self.assertEqual(int(zlink.ContextOption.MAX_SOCKETS), 2)
        self.assertEqual(int(zlink.ContextOption.MAX_MSGSZ), 5)
        self.assertEqual(int(zlink.ContextOption.THREAD_NAME_PREFIX), 9)

    def test_socket_option_values(self):
        self.assertEqual(int(zlink.SocketOption.LINGER), 17)
        self.assertEqual(int(zlink.SocketOption.SNDHWM), 23)
        self.assertEqual(int(zlink.SocketOption.RCVHWM), 24)
        self.assertEqual(int(zlink.SocketOption.SUBSCRIBE), 6)
        self.assertEqual(int(zlink.SocketOption.TLS_CERT), 95)
        self.assertEqual(int(zlink.SocketOption.TLS_PASSWORD), 102)
        self.assertEqual(int(zlink.SocketOption.ZMP_METADATA), 117)

    def test_send_flag_values(self):
        self.assertEqual(int(zlink.SendFlag.NONE), 0)
        self.assertEqual(int(zlink.SendFlag.DONTWAIT), 1)
        self.assertEqual(int(zlink.SendFlag.SNDMORE), 2)

    def test_receive_flag_values(self):
        self.assertEqual(int(zlink.ReceiveFlag.NONE), 0)
        self.assertEqual(int(zlink.ReceiveFlag.DONTWAIT), 1)

    def test_monitor_event_values(self):
        self.assertEqual(int(zlink.MonitorEvent.CONNECTED), 0x0001)
        self.assertEqual(int(zlink.MonitorEvent.DISCONNECTED), 0x0200)
        self.assertEqual(int(zlink.MonitorEvent.ALL), 0xFFFF)

    def test_disconnect_reason_values(self):
        self.assertEqual(int(zlink.DisconnectReason.UNKNOWN), 0)
        self.assertEqual(int(zlink.DisconnectReason.CTX_TERM), 5)

    def test_poll_event_values(self):
        self.assertEqual(int(zlink.PollEvent.POLLIN), 1)
        self.assertEqual(int(zlink.PollEvent.POLLOUT), 2)
        self.assertEqual(int(zlink.PollEvent.POLLERR), 4)
        self.assertEqual(int(zlink.PollEvent.POLLPRI), 8)

    def test_service_type_values(self):
        self.assertEqual(int(zlink.ServiceType.GATEWAY), 1)
        self.assertEqual(int(zlink.ServiceType.SPOT), 2)

    def test_gateway_lb_strategy_values(self):
        self.assertEqual(int(zlink.GatewayLbStrategy.ROUND_ROBIN), 0)
        self.assertEqual(int(zlink.GatewayLbStrategy.WEIGHTED), 1)

    def test_registry_socket_role_values(self):
        self.assertEqual(int(zlink.RegistrySocketRole.PUB), 1)
        self.assertEqual(int(zlink.RegistrySocketRole.ROUTER), 2)
        self.assertEqual(int(zlink.RegistrySocketRole.PEER_SUB), 3)

    def test_discovery_socket_role_values(self):
        self.assertEqual(int(zlink.DiscoverySocketRole.SUB), 1)

    def test_gateway_socket_role_values(self):
        self.assertEqual(int(zlink.GatewaySocketRole.ROUTER), 1)

    def test_receiver_socket_role_values(self):
        self.assertEqual(int(zlink.ReceiverSocketRole.ROUTER), 1)
        self.assertEqual(int(zlink.ReceiverSocketRole.DEALER), 2)

    def test_spot_node_socket_role_values(self):
        self.assertEqual(int(zlink.SpotNodeSocketRole.PUB), 1)
        self.assertEqual(int(zlink.SpotNodeSocketRole.SUB), 2)
        self.assertEqual(int(zlink.SpotNodeSocketRole.DEALER), 3)

    def test_spot_socket_role_values(self):
        self.assertEqual(int(zlink.SpotSocketRole.PUB), 1)
        self.assertEqual(int(zlink.SpotSocketRole.SUB), 2)


class EnumTypeTests(unittest.TestCase):
    """Verify IntEnum/IntFlag are proper int subclasses."""

    def test_int_enum_is_int(self):
        self.assertIsInstance(zlink.SocketType.PAIR, int)
        self.assertIsInstance(zlink.ServiceType.GATEWAY, int)
        self.assertIsInstance(zlink.SocketOption.LINGER, int)

    def test_int_flag_is_int(self):
        self.assertIsInstance(zlink.SendFlag.DONTWAIT, int)
        self.assertIsInstance(zlink.MonitorEvent.CONNECTED, int)
        self.assertIsInstance(zlink.PollEvent.POLLIN, int)

    def test_flag_or_operation(self):
        combined = zlink.SendFlag.DONTWAIT | zlink.SendFlag.SNDMORE
        self.assertEqual(int(combined), 3)
        self.assertIsInstance(combined, int)

    def test_monitor_event_or_operation(self):
        combined = zlink.MonitorEvent.CONNECTED | zlink.MonitorEvent.DISCONNECTED
        self.assertEqual(int(combined), 0x0201)

    def test_poll_event_or_operation(self):
        combined = zlink.PollEvent.POLLIN | zlink.PollEvent.POLLOUT
        self.assertEqual(int(combined), 3)


class BackwardCompatTests(unittest.TestCase):
    """Verify backward-compatible aliases work."""

    def test_service_type_aliases(self):
        self.assertEqual(zlink.SERVICE_TYPE_GATEWAY, 1)
        self.assertEqual(zlink.SERVICE_TYPE_SPOT, 2)
        self.assertEqual(zlink.SERVICE_TYPE_GATEWAY, zlink.ServiceType.GATEWAY)
        self.assertEqual(zlink.SERVICE_TYPE_SPOT, zlink.ServiceType.SPOT)

    def test_enum_usable_as_int_parameter(self):
        """IntEnum values can be passed wherever int is expected."""
        self.assertEqual(zlink.SocketType.PAIR + 1, 1)
        self.assertEqual(zlink.SocketOption.LINGER * 2, 34)


class SocketCreationTests(unittest.TestCase):
    """Verify enum can be used to create sockets."""

    def test_create_pair_socket_with_enum(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")
        s = zlink.Socket(ctx, zlink.SocketType.PAIR)
        s.close()
        ctx.close()


if __name__ == "__main__":
    unittest.main()
