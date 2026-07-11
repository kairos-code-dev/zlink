using System.Buffers.Binary;

namespace Zlink.Framework.Runtime.Streams;

internal static class ZLinkStreamFrameCodec
{
    // Keep byte-compatible with Systems.Zlink.Stream.Connector framing; StreamWireInteropTests is the drift gate.
    internal const int PrefixSize = 6;

    public static byte[] Encode(
        ReadOnlySpan<byte> header,
        ReadOnlySpan<byte> payload)
    {
        var frame = new byte[PrefixSize + header.Length + payload.Length];
        BinaryPrimitives.WriteUInt16BigEndian(frame.AsSpan(0, 2), (ushort)header.Length);
        BinaryPrimitives.WriteUInt32BigEndian(frame.AsSpan(2, 4), (uint)payload.Length);
        header.CopyTo(frame.AsSpan(PrefixSize, header.Length));
        payload.CopyTo(frame.AsSpan(PrefixSize + header.Length, payload.Length));
        return frame;
    }

    public static bool TryDecode(
        ReadOnlySpan<byte> frame,
        out ReadOnlySpan<byte> header,
        out ReadOnlySpan<byte> payload)
    {
        header = default;
        payload = default;
        if (frame.Length < PrefixSize) return false;

        var headerSize = BinaryPrimitives.ReadUInt16BigEndian(frame[..2]);
        var payloadSize = BinaryPrimitives.ReadUInt32BigEndian(frame.Slice(2, 4));
        if (payloadSize > int.MaxValue) return false;

        var totalSize = PrefixSize + headerSize + (int)payloadSize;
        if (frame.Length != totalSize) return false;

        header = frame.Slice(PrefixSize, headerSize);
        payload = frame.Slice(PrefixSize + headerSize, (int)payloadSize);
        return true;
    }
}
