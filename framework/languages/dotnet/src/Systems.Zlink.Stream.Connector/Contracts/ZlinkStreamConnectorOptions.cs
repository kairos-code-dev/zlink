namespace Systems.Zlink.Stream.Connector.Contracts;

public sealed class ZlinkStreamConnectorOptions
{
    public required Uri Endpoint { get; init; }

    public ZlinkStreamTransport? Transport { get; init; }

    public TimeSpan ConnectTimeout { get; init; } = TimeSpan.FromSeconds(5);

    public TimeSpan RequestTimeout { get; init; } = TimeSpan.FromSeconds(30);

    public ZlinkStreamHeartbeatOptions Heartbeat { get; init; } = new();

    public ZlinkStreamReconnectOptions Reconnect { get; init; } = new();

    public int MaxSendPayloadSize { get; init; } = 64 * 1024;

    public bool SkipServerCertificateValidation { get; init; }

    public ZlinkStreamCompression Compression { get; init; } = ZlinkStreamCompression.None;

    public IZlinkStreamHeaderCodec HeaderCodec { get; init; } = ZlinkStreamDefaultCodecs.Header();

    public IZlinkStreamPacketNameResolver NameResolver { get; init; } = ZlinkStreamDefaultCodecs.PacketNameResolver();
}

public sealed class ZlinkStreamHeartbeatOptions
{
    public bool Enabled { get; init; } = true;

    public TimeSpan Interval { get; init; } = TimeSpan.FromSeconds(1);

    public TimeSpan Timeout { get; init; } = TimeSpan.FromSeconds(5);
}

public sealed class ZlinkStreamReconnectOptions
{
    public bool Enabled { get; init; } = true;

    public TimeSpan InitialDelay { get; init; } = TimeSpan.FromMilliseconds(250);

    public TimeSpan MaxDelay { get; init; } = TimeSpan.FromSeconds(5);

    public double BackoffFactor { get; init; } = 2.0;

    public int? MaxAttempts { get; init; } = 3;
}
