import unittest

import zlink


class BoundaryValidationContractTests(unittest.TestCase):
    def test_routing_id_byte_boundaries_are_checked_before_native_call(self):
        self.assertEqual(zlink.RoutingId.from_bytes(b"a" * 255).size, 255)

        for invalid in (b"", b"a" * 256):
            with self.assertRaises(ValueError):
                zlink.RoutingId.from_bytes(invalid)

    def test_endpoint_and_channel_name_reject_fixed_buffer_overflow(self):
        with zlink.Context() as ctx:
            with zlink.PairSocket(ctx) as socket:
                socket.set_channel_name(b"c" * 255)
                self.assertEqual(socket.get_channel_name(), "c" * 255)

                with self.assertRaises(ValueError):
                    socket.set_channel_name(b"c" * 256)
                with self.assertRaises(ValueError):
                    socket.set_channel_name(b"bad\0channel")
                with self.assertRaises(ValueError):
                    socket.bind("inproc://" + ("x" * 248))
                with self.assertRaises(ValueError):
                    socket.bind("inproc://bad\0endpoint")

    def test_typed_numeric_options_reject_native_width_overflow(self):
        with zlink.Context() as ctx:
            with self.assertRaises(OverflowError):
                ctx.options.io_threads = 1 << 31
            with self.assertRaises(OverflowError):
                ctx.options.io_threads = -(1 << 31) - 1

            with zlink.PairSocket(ctx) as socket:
                socket.options.max_msg_size = (1 << 63) - 1
                self.assertEqual(socket.options.max_msg_size, (1 << 63) - 1)

                with self.assertRaises(OverflowError):
                    socket.options.max_msg_size = 1 << 63
                with self.assertRaises(OverflowError):
                    socket.options.max_msg_size = -(1 << 63) - 1


class OwnershipContractTests(unittest.TestCase):
    def test_message_copy_owns_bytes_independently_of_source_buffer(self):
        source = bytearray(b"payload")
        with zlink.Message.copy_from(source) as message:
            source[:] = b"changed"
            self.assertEqual(message.to_bytes(), b"payload")

        self.assertEqual(message.size(), 0)
        message.close()
        self.assertEqual(message.to_bytes(), b"")

    def test_caller_provided_received_storage_close_is_idempotent_when_empty(self):
        received = zlink.Received()

        self.assertEqual(len(received), 0)
        with self.assertRaises(zlink.RecvError) as raised:
            received.single_part_or_throw()
        self.assertEqual(raised.exception.result, zlink.RecvResult.NO_DATA)

        received.close()
        received.close()


if __name__ == "__main__":
    unittest.main()
