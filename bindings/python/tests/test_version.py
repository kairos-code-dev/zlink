import unittest

import zlink


class VersionTests(unittest.TestCase):
    def test_version_matches_core(self):
        try:
            major, minor, patch = zlink.version()
        except OSError:
            self.skipTest("zlink native library not found")
        self.assertEqual(major, 0)
        self.assertEqual(minor, 6)
        self.assertEqual(patch, 0)


if __name__ == "__main__":
    unittest.main()
