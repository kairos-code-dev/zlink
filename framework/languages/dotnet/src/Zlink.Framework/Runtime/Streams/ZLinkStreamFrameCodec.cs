using System.Buffers.Binary;

namespace Zlink.Framework.Runtime.Streams;

internal static class ZLinkStreamFrameCodec
{
    public static byte[] Encode(
        ReadOnlySpan<byte> header,
        ReadOnlySpan<byte> body)
    {
        var frame = new byte[6 + header.Length + body.Length];
        BinaryPrimitives.WriteUInt16BigEndian(frame.AsSpan(0, 2), (ushort)header.Length);
        BinaryPrimitives.WriteUInt32BigEndian(frame.AsSpan(2, 4), (uint)body.Length);
        header.CopyTo(frame.AsSpan(6, header.Length));
        body.CopyTo(frame.AsSpan(6 + header.Length, body.Length));
        return frame;
    }
}
