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
        self.assertTrue(hasattr(native, "zlink_service_monitor_open"))
        self.assertTrue(hasattr(native, "zlink_try_send"))
        self.assertTrue(hasattr(native, "zlink_try_send_rid"))
        self.assertTrue(hasattr(native, "zlink_try_publish"))
        self.assertTrue(hasattr(native, "zlink_registry_bind"))
        self.assertTrue(hasattr(native, "zlink_discovery_new"))
        self.assertTrue(hasattr(native, "zlink_spot_new"))

        for removed_name in (
            "zlink_setsockopt",
            "zlink_getsockopt",
            "zlink_msg_send",
            "zlink_msg_send_rid",
            "zlink_msg_recv",
            "zlink_msg_recv_rid",
            "zlink_monitor_recv",
            "zlink_discovery_new_typed",
            "zlink_spot_pub_new",
            "zlink_spot_sub_new",
        ):
            with self.assertRaises(AttributeError):
                getattr(native, removed_name)
