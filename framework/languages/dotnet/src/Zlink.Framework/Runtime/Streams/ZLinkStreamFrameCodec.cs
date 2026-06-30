using System.Buffers.Binary;

namespace Zlink.Framework.Runtime.Streams;

internal static class ZLinkStreamFrameCodec
{
    public static byte[] Encode(
        ReadOnlySpan<byte> header,
        ReadOnlySpan<byte> payload)
    {
        var frame = new byte[6 + header.Length + payload.Length];
        BinaryPrimitives.WriteUInt16BigEndian(frame.AsSpan(0, 2), (ushort)header.Length);
        BinaryPrimitives.WriteUInt32BigEndian(frame.AsSpan(2, 4), (uint)payload.Length);
        header.CopyTo(frame.AsSpan(6, header.Length));
        payload.CopyTo(frame.AsSpan(6 + header.Length, payload.Length));
        return frame;
    }
}