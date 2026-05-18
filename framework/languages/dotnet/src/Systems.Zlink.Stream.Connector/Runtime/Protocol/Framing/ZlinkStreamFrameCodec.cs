using System.Buffers.Binary;

namespace Systems.Zlink.Stream.Connector.Runtime.Protocol.Framing;

internal readonly record struct ZlinkStreamFrame(
    ReadOnlyMemory<byte> Header,
    ReadOnlyMemory<byte> Payload);

internal static class ZlinkStreamFrameCodec
{
    public static void ValidateSendFrame(int headerLength, int payloadLength, int maxFrameSize)
    {
        if (headerLength > ushort.MaxValue)
        {
            throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameTooLarge, "Header exceeds u16 header_size.");
        }

        var totalSize = checked(2 + 4 + headerLength + payloadLength);
        if (totalSize > maxFrameSize)
        {
            throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameTooLarge, "Frame exceeds MaxSendFrameSize.");
        }
    }

    public static ReadOnlyMemory<byte> EncodePrefix(int headerLength, int payloadLength)
    {
        ValidateSendFrame(headerLength, payloadLength, int.MaxValue);
        var prefix = new byte[6];
        WritePrefix(prefix, headerLength, payloadLength);
        return prefix;
    }

    public static void WritePrefix(
        Span<byte> destination,
        int headerLength,
        int payloadLength)
    {
        if (destination.Length < 6)
        {
            throw new ArgumentException("Frame prefix destination must be at least 6 bytes.", nameof(destination));
        }

        ValidateSendFrame(headerLength, payloadLength, int.MaxValue);
        BinaryPrimitives.WriteUInt16BigEndian(destination[..2], (ushort)headerLength);
        BinaryPrimitives.WriteUInt32BigEndian(destination.Slice(2, 4), (uint)payloadLength);
    }

    public static ReadOnlyMemory<byte> Encode(
        ReadOnlyMemory<byte> header,
        ReadOnlyMemory<byte> payload,
        int maxFrameSize)
    {
        ValidateSendFrame(header.Length, payload.Length, maxFrameSize);

        var totalSize = checked(2 + 4 + header.Length + payload.Length);
        var frame = new byte[totalSize];
        WriteFrame(frame, header, payload, maxFrameSize);
        return frame;
    }

    public static int GetFrameSize(int headerLength, int payloadLength, int maxFrameSize)
    {
        ValidateSendFrame(headerLength, payloadLength, maxFrameSize);
        return checked(2 + 4 + headerLength + payloadLength);
    }

    public static void WriteFrame(
        Span<byte> destination,
        ReadOnlyMemory<byte> header,
        ReadOnlyMemory<byte> payload,
        int maxFrameSize)
    {
        var totalSize = GetFrameSize(header.Length, payload.Length, maxFrameSize);
        if (destination.Length < totalSize)
        {
            throw new ArgumentException("Frame destination is smaller than the encoded frame.", nameof(destination));
        }

        WritePrefix(destination[..6], header.Length, payload.Length);
        header.Span.CopyTo(destination[6..]);
        payload.Span.CopyTo(destination[(6 + header.Length)..]);
    }

    public static async ValueTask<ZlinkStreamFrame> ReadAsync(
        IZlinkStreamConnection connection,
        CancellationToken cancellationToken)
    {
        var prefix = new byte[6];
        await ReadExactAsync(connection, prefix, cancellationToken).ConfigureAwait(false);
        var headerSize = BinaryPrimitives.ReadUInt16BigEndian(prefix.AsSpan(0, 2));
        var payloadSize = BinaryPrimitives.ReadUInt32BigEndian(prefix.AsSpan(2, 4));
        if (payloadSize > int.MaxValue)
        {
            throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameTooLarge, "Payload exceeds supported in-memory size.");
        }

        var header = new byte[headerSize];
        var payload = new byte[(int)payloadSize];
        await ReadExactAsync(connection, header, cancellationToken).ConfigureAwait(false);
        await ReadExactAsync(connection, payload, cancellationToken).ConfigureAwait(false);
        return new ZlinkStreamFrame(header, payload);
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
