using System.Buffers.Binary;

namespace Systems.Zlink.Stream.Connector.Runtime.Protocol.Framing;

internal readonly record struct ZlinkStreamFrame(
    ReadOnlyMemory<byte> Header,
    ReadOnlyMemory<byte> Body);

internal static class ZlinkStreamFrameCodec
{
    public static void ValidateSendFrame(int headerLength, int bodyLength, int maxFrameSize)
    {
        if (headerLength > ushort.MaxValue)
        {
            throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameTooLarge, "Header exceeds u16 header_size.");
        }

        var totalSize = checked(2 + 4 + headerLength + bodyLength);
        if (totalSize > maxFrameSize)
        {
            throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameTooLarge, "Frame exceeds MaxSendFrameSize.");
        }
    }

    public static ReadOnlyMemory<byte> EncodePrefix(int headerLength, int bodyLength)
    {
        ValidateSendFrame(headerLength, bodyLength, int.MaxValue);
        var prefix = new byte[6];
        WritePrefix(prefix, headerLength, bodyLength);
        return prefix;
    }

    public static void WritePrefix(
        Span<byte> destination,
        int headerLength,
        int bodyLength)
    {
        if (destination.Length < 6)
        {
            throw new ArgumentException("Frame prefix destination must be at least 6 bytes.", nameof(destination));
        }

        ValidateSendFrame(headerLength, bodyLength, int.MaxValue);
        BinaryPrimitives.WriteUInt16BigEndian(destination[..2], (ushort)headerLength);
        BinaryPrimitives.WriteUInt32BigEndian(destination.Slice(2, 4), (uint)bodyLength);
    }

    public static ReadOnlyMemory<byte> Encode(
        ReadOnlyMemory<byte> header,
        ReadOnlyMemory<byte> body,
        int maxFrameSize)
    {
        ValidateSendFrame(header.Length, body.Length, maxFrameSize);

        var totalSize = checked(2 + 4 + header.Length + body.Length);
        var frame = new byte[totalSize];
        WriteFrame(frame, header, body, maxFrameSize);
        return frame;
    }

    public static int GetFrameSize(int headerLength, int bodyLength, int maxFrameSize)
    {
        ValidateSendFrame(headerLength, bodyLength, maxFrameSize);
        return checked(2 + 4 + headerLength + bodyLength);
    }

    public static void WriteFrame(
        Span<byte> destination,
        ReadOnlyMemory<byte> header,
        ReadOnlyMemory<byte> body,
        int maxFrameSize)
    {
        var totalSize = GetFrameSize(header.Length, body.Length, maxFrameSize);
        if (destination.Length < totalSize)
        {
            throw new ArgumentException("Frame destination is smaller than the encoded frame.", nameof(destination));
        }

        WritePrefix(destination[..6], header.Length, body.Length);
        header.Span.CopyTo(destination[6..]);
        body.Span.CopyTo(destination[(6 + header.Length)..]);
    }

    public static async ValueTask<ZlinkStreamFrame> ReadAsync(
        IZlinkStreamConnection connection,
        CancellationToken cancellationToken)
    {
        var prefix = new byte[6];
        await ReadExactAsync(connection, prefix, cancellationToken).ConfigureAwait(false);
        var headerSize = BinaryPrimitives.ReadUInt16BigEndian(prefix.AsSpan(0, 2));
        var bodySize = BinaryPrimitives.ReadUInt32BigEndian(prefix.AsSpan(2, 4));
        if (bodySize > int.MaxValue)
        {
            throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameTooLarge, "Body exceeds supported in-memory size.");
        }

        var header = new byte[headerSize];
        var body = new byte[(int)bodySize];
        await ReadExactAsync(connection, header, cancellationToken).ConfigureAwait(false);
        await ReadExactAsync(connection, body, cancellationToken).ConfigureAwait(false);
        return new ZlinkStreamFrame(header, body);
    }

    private static async ValueTask ReadExactAsync(
        IZlinkStreamConnection connection,
        Memory<byte> buffer,
        CancellationToken cancellationToken)
    {
        var read = 0;
        while (read < buffer.Length)
        {
            var count = await connection.ReadAsync(buffer.Slice(read), cancellationToken).ConfigureAwait(false);
            if (count == 0)
            {
                throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.Disconnected, "Remote stream closed.");
            }

            read += count;
        }
    }
}
