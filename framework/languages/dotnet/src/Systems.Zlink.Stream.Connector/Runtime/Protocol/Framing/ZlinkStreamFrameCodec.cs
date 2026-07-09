using System.Buffers.Binary;

namespace Systems.Zlink.Stream.Connector.Runtime.Protocol.Framing;

internal readonly record struct ZlinkStreamFrame(
    ReadOnlyMemory<byte> Header,
    ReadOnlyMemory<byte> Payload);

internal static class ZlinkStreamFrameCodec
{
    // Keep byte-compatible with Zlink.Framework stream framing; StreamWireInteropTests is the drift gate.
    public static void ValidateSendFrame(int headerLength, int payloadLength)
    {
        if (headerLength > ushort.MaxValue)
            throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameTooLarge, "Header exceeds u16 header_size.");

        _ = checked(2 + 4 + headerLength + payloadLength);
    }

    public static void WritePrefix(
        Span<byte> destination,
        int headerLength,
        int payloadLength)
    {
        if (destination.Length < 6)
            throw new ArgumentException("Frame prefix destination must be at least 6 bytes.", nameof(destination));

        ValidateSendFrame(headerLength, payloadLength);
        BinaryPrimitives.WriteUInt16BigEndian(destination[..2], (ushort)headerLength);
        BinaryPrimitives.WriteUInt32BigEndian(destination.Slice(2, 4), (uint)payloadLength);
    }

    public static int GetFrameSize(int headerLength, int payloadLength)
    {
        ValidateSendFrame(headerLength, payloadLength);
        return checked(2 + 4 + headerLength + payloadLength);
    }

    public static void WriteFrame(
        Span<byte> destination,
        ReadOnlyMemory<byte> header,
        ReadOnlyMemory<byte> payload)
    {
        var totalSize = GetFrameSize(header.Length, payload.Length);
        if (destination.Length < totalSize)
            throw new ArgumentException("Frame destination is smaller than the encoded frame.", nameof(destination));

        WritePrefix(destination[..6], header.Length, payload.Length);
        header.Span.CopyTo(destination[6..]);
        payload.Span.CopyTo(destination[(6 + header.Length)..]);
    }

    public static void ValidateSendPayload(int payloadLength, int maxPayloadSize)
    {
        if (payloadLength > maxPayloadSize)
            throw ZlinkStreamConnector.Error(
                ZlinkStreamErrorCode.FrameTooLarge,
                "Payload exceeds MaxSendPayloadSize.");
    }

    public static async ValueTask<ZlinkStreamFrame> ReadAsync(
        IZlinkStreamConnection connection,
        int maxPayloadSize,
        CancellationToken cancellationToken)
    {
        var prefix = new byte[6];
        await ReadExactAsync(connection, prefix, cancellationToken).ConfigureAwait(false);
        var headerSize = BinaryPrimitives.ReadUInt16BigEndian(prefix.AsSpan(0, 2));
        var payloadSize = BinaryPrimitives.ReadUInt32BigEndian(prefix.AsSpan(2, 4));
        ValidateReceivePayload(payloadSize, maxPayloadSize);
        var bodySize = checked((long)headerSize + payloadSize);
        if (bodySize > int.MaxValue)
            throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameTooLarge,
                "Frame body exceeds supported in-memory size.");

        var header = new byte[headerSize];
        var payload = new byte[(int)payloadSize];
        await ReadExactAsync(connection, header, cancellationToken).ConfigureAwait(false);
        await ReadExactAsync(connection, payload, cancellationToken).ConfigureAwait(false);
        return new ZlinkStreamFrame(header, payload);
    }

    public static void ValidateReceivePayload(uint payloadLength, int maxPayloadSize)
    {
        if (payloadLength > (uint)maxPayloadSize)
            throw ZlinkStreamConnector.Error(
                ZlinkStreamErrorCode.FrameTooLarge,
                "Payload exceeds MaxReceivePayloadSize.");
    }

    public static long GetMaxReceiveFrameSize(int maxPayloadSize)
    {
        return 6L + ushort.MaxValue + maxPayloadSize;
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
                throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.Disconnected, "Remote stream closed.");

            read += count;
        }
    }
}
