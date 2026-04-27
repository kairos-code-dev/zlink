import unittest

import zlink
from zlink._ffi import lib


class NativeContractTests(unittest.TestCase):
    def test_import_does_not_require_removed_legacy_symbols(self):
        try:
            native = lib()
        except OSError:
            self.skipTest("zlink native library not found")

        self.assertTrue(hasattr(native, "zlink_set_option"))
        self.assertTrue(hasattr(native, "zlink_get_option"))
        self.assertTrue(hasattr(native, "zlink_socket_monitor_open"))
        self.assertTrue(hasattr(native, "zlink_socket_monitor_handler"))
        self.assertTrue(hasattr(native, "zlink_registry_bind"))
        self.assertTrue(hasattr(native, "zlink_registry_status_snapshot"))
        self.assertTrue(hasattr(native, "zlink_registry_service_summary_snapshot"))
        self.assertTrue(hasattr(native, "zlink_registry_member_peers"))
        self.assertTrue(hasattr(native, "zlink_registry_topology_query"))
        self.assertTrue(hasattr(native, "zlink_discovery_new"))
        self.assertTrue(hasattr(native, "zlink_spot_new"))
        self.assertTrue(hasattr(native, "zlink_registry_query_snapshot"))

        for removed_name in (
            "zlink_setsockopt",
            "zlink_getsockopt",
            "zlink_msg_send",
            "zlink_msg_send_rid",
            "zlink_msg_recv",
            "zlink_msg_recv_rid",
            "zlink_monitor_recv",
            "zlink_service_monitor_open",
            "zlink_service_monitor_handler",
            "zlink_service_monitor_recv",
            "zlink_try_send",
            "zlink_try_send_rid",
            "zlink_try_publish",
            "zlink_discovery_new_typed",
            "zlink_spot_pub_new",
            "zlink_spot_sub_new",
        ):
            with self.assertRaises(AttributeError):
                getattr(native, removed_name)
