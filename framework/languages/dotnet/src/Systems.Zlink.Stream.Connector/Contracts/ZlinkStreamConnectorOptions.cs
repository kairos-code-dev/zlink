namespace Systems.Zlink.Stream.Connector.Contracts;

public sealed class ZlinkStreamConnectorOptions
{
    public required Uri Endpoint { get; init; }

    public ZlinkStreamTransport? Transport { get; init; }

    public TimeSpan ConnectTimeout { get; init; } = TimeSpan.FromSeconds(5);

    public TimeSpan RequestTimeout { get; init; } = TimeSpan.FromSeconds(30);

    public TimeSpan HeartbeatInterval { get; init; } = TimeSpan.FromSeconds(10);

    public TimeSpan HeartbeatTimeout { get; init; } = TimeSpan.FromSeconds(30);

    public TimeSpan IdleTimeout { get; init; } = TimeSpan.FromSeconds(60);

    public int MaxSendFrameSize { get; init; } = 1024 * 1024;

    public int MaxSendMetadataSize { get; init; } = 1024;

    public int HandlerQueueCapacity { get; init; } = 4096;

    public bool SkipServerCertificateValidation { get; init; }

    public bool EnableSegmentedSend { get; init; } = true;

    public ZlinkStreamCompression Compression { get; init; } = ZlinkStreamCompression.None;

    public IZlinkStreamHeaderCodec? HeaderCodec { get; init; }

    public IZlinkStreamCompressionCodec? CompressionCodec { get; init; }

    public IZlinkStreamPacketNameResolver? NameResolver { get; init; }
}
