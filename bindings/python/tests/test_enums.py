import unittest

import zlink


class EnumValueTests(unittest.TestCase):
    """Verify enum values match C header #define constants."""

    def test_socket_type_values(self):
        self.assertEqual(int(zlink.SocketType.PAIR), 0x1001)
        self.assertEqual(int(zlink.SocketType.PUB), 0x1002)
        self.assertEqual(int(zlink.SocketType.SUB), 0x1003)
        self.assertEqual(int(zlink.SocketType.DEALER), 0x1004)
        self.assertEqual(int(zlink.SocketType.ROUTER), 0x1005)
        self.assertEqual(int(zlink.SocketType.XPUB), 0x1006)
        self.assertEqual(int(zlink.SocketType.XSUB), 0x1007)
        self.assertEqual(int(zlink.SocketType.STREAM), 0x1008)

    def test_context_option_values(self):
        self.assertEqual(int(zlink.ContextOption.IO_THREADS), 1)
        self.assertEqual(int(zlink.ContextOption.MAX_SOCKETS), 2)
        self.assertEqual(int(zlink.ContextOption.MAX_MSGSZ), 5)
        self.assertEqual(int(zlink.ContextOption.THREAD_NAME_PREFIX), 9)

    def test_socket_option_values(self):
        self.assertEqual(int(zlink.SocketOption.LINGER), 0x300A)
        self.assertEqual(int(zlink.SocketOption.SNDHWM), 0x300F)
        self.assertEqual(int(zlink.SocketOption.RCVHWM), 0x3010)
        self.assertEqual(int(zlink.SocketOption.TLS_CERT), 0x3028)
        self.assertEqual(int(zlink.SocketOption.TLS_PASSWORD), 0x302F)
        self.assertEqual(int(zlink.SocketOption.ZMP_METADATA), 0x3030)

    def test_legacy_socket_option_aliases(self):
        self.assertEqual(int(zlink.SocketOption.ROUTING_ID), 5)
        self.assertEqual(int(zlink.SocketOption.SUBSCRIBE), 6)
        self.assertEqual(int(zlink.SocketOption.UNSUBSCRIBE), 7)

    def test_send_result_values(self):
        self.assertEqual(int(zlink.SendResult.SENT), 0)
        self.assertEqual(int(zlink.SendResult.BACKPRESSURED), 1)
        self.assertEqual(int(zlink.SendResult.NOT_READY), 2)

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

    def test_error_code_values(self):
        self.assertEqual(int(zlink.ErrorCode.EFSM), 156384763)
        self.assertEqual(int(zlink.ErrorCode.ENOCOMPATPROTO), 156384764)
        self.assertEqual(int(zlink.ErrorCode.ETERM), 156384765)
        self.assertEqual(int(zlink.ErrorCode.EMTHREAD), 156384766)

    def test_protocol_error_values(self):
        self.assertEqual(
            int(zlink.ProtocolError.ZMP_MALFORMED_COMMAND_HELLO), 0x10000013
        )

    def test_service_type_values(self):
        self.assertEqual(int(zlink.ServiceType.SPOT), 0x3002)
        self.assertEqual(int(zlink.ServiceType.SOCKET), 0x3003)

    def test_registry_socket_role_values(self):
        self.assertEqual(int(zlink.RegistrySocketRole.PUB), 1)
        self.assertEqual(int(zlink.RegistrySocketRole.ROUTER), 2)
        self.assertEqual(int(zlink.RegistrySocketRole.PEER_SUB), 3)

    def test_spot_node_socket_role_values(self):
        self.assertEqual(int(zlink.SpotNodeSocketRole.NODE), 0)
        self.assertEqual(int(zlink.SpotNodeSocketRole.PUB), 1)
        self.assertEqual(int(zlink.SpotNodeSocketRole.SUB), 2)
        self.assertEqual(int(zlink.SpotNodeSocketRole.DEALER), 3)

    def test_spot_node_option_values(self):
        self.assertEqual(int(zlink.SpotNodeOption.PUB_MODE), 1)
        self.assertEqual(int(zlink.SpotNodeOption.PUB_QUEUE_HWM), 2)
        self.assertEqual(int(zlink.SpotNodeOption.PUB_QUEUE_FULL_POLICY), 3)

    def test_spot_node_pub_mode_values(self):
        self.assertEqual(int(zlink.SpotNodePubMode.SYNC), 0)
        self.assertEqual(int(zlink.SpotNodePubMode.ASYNC), 1)

    def test_spot_node_pub_queue_full_policy_values(self):
        self.assertEqual(int(zlink.SpotNodePubQueueFullPolicy.EAGAIN), 0)
        self.assertEqual(int(zlink.SpotNodePubQueueFullPolicy.DROP), 1)

    def test_spot_socket_role_values(self):
        self.assertEqual(int(zlink.SpotSocketRole.PUB), 1)
        self.assertEqual(int(zlink.SpotSocketRole.SUB), 2)


class EnumTypeTests(unittest.TestCase):
    """Verify IntEnum/IntFlag are proper int subclasses."""

    def test_int_enum_is_int(self):
        self.assertIsInstance(zlink.SocketType.PAIR, int)
        self.assertIsInstance(zlink.ServiceType.SPOT, int)
        self.assertIsInstance(zlink.SocketOption.LINGER, int)

    def test_int_flag_is_int(self):
        self.assertIsInstance(zlink.SendResult.SENT, int)
        self.assertIsInstance(zlink.MonitorEvent.CONNECTED, int)
        self.assertIsInstance(zlink.PollEvent.POLLIN, int)

    def test_monitor_event_or_operation(self):
        combined = zlink.MonitorEvent.CONNECTED | zlink.MonitorEvent.DISCONNECTED
        self.assertEqual(int(combined), 0x0201)

    def test_poll_event_or_operation(self):
        combined = zlink.PollEvent.POLLIN | zlink.PollEvent.POLLOUT
        self.assertEqual(int(combined), 3)


class BackwardCompatTests(unittest.TestCase):
    """Verify backward-compatible aliases work."""

    def test_service_type_aliases(self):
        self.assertEqual(zlink.SERVICE_TYPE_SPOT, 0x3002)
        self.assertEqual(zlink.SERVICE_TYPE_SPOT, zlink.ServiceType.SPOT)

    def test_enum_usable_as_int_parameter(self):
        """IntEnum values can be passed wherever int is expected."""
        self.assertEqual(zlink.SocketType.PAIR + 1, 0x1002)
        self.assertEqual(zlink.SocketOption.LINGER * 2, 0x6014)


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
