import unittest

import zlink


class NativeContractTests(unittest.TestCase):
    def test_import_does_not_require_removed_legacy_symbols(self):
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
            self.assertFalse(hasattr(zlink, removed_name))
