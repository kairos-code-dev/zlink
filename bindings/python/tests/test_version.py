import pathlib
import re
import unittest

import zlink


class VersionTests(unittest.TestCase):
    def test_version_matches_core(self):
        try:
            major, minor, patch = zlink.version()
        except OSError:
            self.skipTest("zlink native library not found")

        header = (
            pathlib.Path(__file__).resolve().parents[3] / "core" / "include" / "zlink.h"
        ).read_text(encoding="utf-8")
        expected_major = int(
            re.search(r"^#define ZLINK_VERSION_MAJOR (\d+)$", header, re.MULTILINE).group(1)
        )
        expected_minor = int(
            re.search(r"^#define ZLINK_VERSION_MINOR (\d+)$", header, re.MULTILINE).group(1)
        )
        expected_patch = int(
            re.search(r"^#define ZLINK_VERSION_PATCH (\d+)$", header, re.MULTILINE).group(1)
        )

        self.assertEqual((major, minor, patch), (expected_major, expected_minor, expected_patch))

    def test_pair_send_recv(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")
        with ctx:
            with zlink.PairSocket(ctx) as s1:
                with zlink.PairSocket(ctx) as s2:
                    endpoint = "inproc://py-pair"
                    s1.bind(endpoint)
                    s2.connect(endpoint)
                    payload = b"ping"
                    s1.send().message(payload).submit()
                    with s2.recv() as received:
                        self.assertEqual(received.to_bytes_list(), [payload])

if __name__ == "__main__":
    unittest.main()
