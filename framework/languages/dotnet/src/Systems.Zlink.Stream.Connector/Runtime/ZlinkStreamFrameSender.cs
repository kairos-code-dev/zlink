using System.Buffers;

namespace Systems.Zlink.Stream.Connector.Runtime;

internal sealed class ZlinkStreamFrameSender(
    ZlinkStreamConnectorOptions options,
    IZlinkStreamHeaderCodec headerCodec,
    IZlinkStreamCompressionCodec? compressionCodec,
    SemaphoreSlim sendGate,
    Func<IZlinkStreamConnection?> connectionProvider)
{
    public ZlinkStreamOutboundFrame BuildOutboundFrame(
        ZlinkStreamMessageKind kind,
        string name,
        ZlinkStreamEncodedPayload payload,
        ZlinkStreamMetadata metadata,
        bool compress,
        ZlinkStreamRequestSeq? requestSeq)
    {
        ZlinkStreamConnector.ValidateName(name);
        var payloadBytes = payload.Payload;
        var flags = requestSeq is null
            ? ZlinkStreamHeaderFlags.None
            : ZlinkStreamHeaderFlags.HasRequestSeq;

        if (compress)
        {
            payloadBytes = CompressPayload(payloadBytes);
            flags |= ZlinkStreamHeaderFlags.PayloadCompressed;
        }

        var header = new ZlinkStreamHeader(kind, payload.Codec, flags, requestSeq, name, metadata);
        return new ZlinkStreamOutboundFrame(header, EncodeHeaderForSend(header), payloadBytes);
    }

    public void ValidateSendReady(ReadOnlyMemory<byte> header, ReadOnlyMemory<byte> payload)
    {
        ZlinkStreamFrameCodec.ValidateSendFrame(header.Length, payload.Length, options.MaxSendFrameSize);
        if (connectionProvider() is null)
        {
            throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.Disconnected, "Connector is not connected.");
        }
    }

    public async ValueTask SendPacketAsync(
        ReadOnlyMemory<byte> header,
        ReadOnlyMemory<byte> payload,
        CancellationToken cancellationToken)
    {
        var connection = connectionProvider();
        if (connection is null)
        {
            throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.Disconnected, "Connector is not connected.");
        }

        try
        {
            await sendGate.WaitAsync(cancellationToken).ConfigureAwait(false);
            try
            {
                if (options.EnableSegmentedSend && connection.CanWriteSegments)
                {
                    var prefix = ArrayPool<byte>.Shared.Rent(6);
                    try
                    {
                        ZlinkStreamFrameCodec.WritePrefix(
                            prefix.AsSpan(0, 6),
                            header.Length,
                            payload.Length);
                        await connection.WriteAsync(
                            prefix.AsMemory(0, 6),
                            cancellationToken).ConfigureAwait(false);
                    }
                    finally
                    {
                        ArrayPool<byte>.Shared.Return(prefix);
                    }

                    if (header.Length > 0)
                    {
                        await connection.WriteAsync(header, cancellationToken).ConfigureAwait(false);
                    }

                    if (payload.Length > 0)
                    {
                        await connection.WriteAsync(payload, cancellationToken).ConfigureAwait(false);
                    }
                }
                else
                {
                    var frameSize = ZlinkStreamFrameCodec.GetFrameSize(
                        header.Length,
                        payload.Length,
                        options.MaxSendFrameSize);
                    var frame = ArrayPool<byte>.Shared.Rent(frameSize);
                    try
                    {
                        ZlinkStreamFrameCodec.WriteFrame(
                            frame.AsSpan(0, frameSize),
                            header,
                            payload,
                            options.MaxSendFrameSize);
                        await connection.WriteAsync(
                            frame.AsMemory(0, frameSize),
                            cancellationToken).ConfigureAwait(false);
                    }
                    finally
                    {
                        ArrayPool<byte>.Shared.Return(frame);
                    }
                }
            }
            finally
            {
                sendGate.Release();
            }
        }
        catch (Exception ex) when (ex is not ZlinkStreamException)
        {
            throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.SendFailed, "Send failed.", ex);
        }
    }

    public ReadOnlyMemory<byte> DecompressIfNeeded(ZlinkStreamHeader header, ReadOnlyMemory<byte> payload)
    {
        if (!header.Flags.HasFlag(ZlinkStreamHeaderFlags.PayloadCompressed))
        {
            return payload;
        }

        if (options.Compression == ZlinkStreamCompression.None || compressionCodec is null)
        {
            throw ZlinkStreamConnector.Error(
                ZlinkStreamErrorCode.FrameDecodeFailed,
                "Compressed payload received without a compression codec.");
        }

        try
        {
            return compressionCodec.Decompress(payload);
        }
        catch (Exception ex)
        {
            throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.DecompressionFailed, "Decompression failed.", ex);
        }
    }

    private ReadOnlyMemory<byte> EncodeHeaderForSend(ZlinkStreamHeader header)
    {
        var encoded = headerCodec.Encode(header);
        var metadataLength = ZlinkStreamHeaderCodec.GetMetadataPayloadSize(header.Metadata);
        if (metadataLength > options.MaxSendMetadataSize)
        {
            throw ZlinkStreamConnector.Error(
                ZlinkStreamErrorCode.ValidationFailed,
                $"Metadata payload exceeds MaxSendMetadataSize ({options.MaxSendMetadataSize}).");
        }

        return encoded;
    }

    private ReadOnlyMemory<byte> CompressPayload(ReadOnlyMemory<byte> payload)
    {
        if (options.Compression == ZlinkStreamCompression.None || compressionCodec is null)
        {
            throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.CompressionFailed, "Compression codec is not configured.");
        }

        try
        {
            return compressionCodec.Compress(payload);
        }
        catch (Exception ex)
        {
            throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.CompressionFailed, "Compression failed.", ex);
        }
    }
}
