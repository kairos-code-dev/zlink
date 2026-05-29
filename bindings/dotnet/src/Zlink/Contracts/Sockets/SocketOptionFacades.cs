// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

/// <summary>
/// Represents common socket options.
/// </summary>
public class CommonSocketOptions
{
    private protected readonly ISocketOptionEndpoint Socket;

    internal CommonSocketOptions(ISocketOptionEndpoint socket)
    {
        Socket = socket;
    }

    internal ulong Affinity
    {
        get => Socket.GetOption(SocketOptions.Affinity);
        set => Socket.SetOption(SocketOptions.Affinity, value);
    }

    internal int Rate
    {
        get => Socket.GetOption(SocketOptions.Rate);
        set => Socket.SetOption(SocketOptions.Rate, value);
    }

    internal TimeSpan? RecoveryInterval
    {
        get => DecodeDuration(Socket.GetOption(SocketOptions.RecoveryIvl));
        set => Socket.SetOption(SocketOptions.RecoveryIvl,
            EncodeDuration(value, nameof(value)));
    }

    /// <summary>
    /// Gets the max message size.
    /// </summary>
    public long MaxMessageSize
    {
        get => Socket.GetOption(SocketOptions.MaxMsgSize);
        set => Socket.SetOption(SocketOptions.MaxMsgSize, value);
    }

    /// <summary>
    /// Starts a send operation.
    /// </summary>
    public int SendHighWaterMark
    {
        get => Socket.GetOption(SocketOptions.SndHwm);
        set => Socket.SetOption(SocketOptions.SndHwm, value);
    }

    /// <summary>
    /// Receives the next available item.
    /// </summary>
    public int ReceiveHighWaterMark
    {
        get => Socket.GetOption(SocketOptions.RcvHwm);
        set => Socket.SetOption(SocketOptions.RcvHwm, value);
    }

    /// <summary>
    /// Starts a send operation.
    /// </summary>
    public int SendBufferSize
    {
        get => Socket.GetOption(SocketOptions.SndBuf);
        set => Socket.SetOption(SocketOptions.SndBuf, value);
    }

    /// <summary>
    /// Receives the next available item.
    /// </summary>
    public int ReceiveBufferSize
    {
        get => Socket.GetOption(SocketOptions.RcvBuf);
        set => Socket.SetOption(SocketOptions.RcvBuf, value);
    }

    /// <summary>
    /// Gets the linger.
    /// </summary>
    public TimeSpan? Linger
    {
        get => DecodeDuration(Socket.GetOption(SocketOptions.Linger));
        set => Socket.SetOption(SocketOptions.Linger,
            EncodeDuration(value, nameof(value)));
    }

    /// <summary>
    /// Gets the reconnect interval.
    /// </summary>
    public TimeSpan? ReconnectInterval
    {
        get => DecodeDuration(Socket.GetOption(SocketOptions.ReconnectIvl));
        set => Socket.SetOption(SocketOptions.ReconnectIvl,
            EncodeDuration(value, nameof(value)));
    }

    /// <summary>
    /// Gets the reconnect interval max.
    /// </summary>
    public TimeSpan? ReconnectIntervalMax
    {
        get => DecodeDuration(Socket.GetOption(SocketOptions.ReconnectIvlMax));
        set => Socket.SetOption(SocketOptions.ReconnectIvlMax,
            EncodeDuration(value, nameof(value)));
    }

    /// <summary>
    /// Gets the backlog.
    /// </summary>
    public int Backlog
    {
        get => Socket.GetOption(SocketOptions.Backlog);
        set => Socket.SetOption(SocketOptions.Backlog, value);
    }

    internal int MulticastHops
    {
        get => Socket.GetOption(SocketOptions.MulticastHops);
        set => Socket.SetOption(SocketOptions.MulticastHops, value);
    }

    /// <summary>
    /// Receives the next available item.
    /// </summary>
    public TimeSpan? ReceiveTimeout
    {
        get => DecodeDuration(Socket.GetOption(SocketOptions.RcvTimeo));
        set => Socket.SetOption(SocketOptions.RcvTimeo,
            EncodeDuration(value, nameof(value)));
    }

    /// <summary>
    /// Starts a send operation.
    /// </summary>
    public TimeSpan? SendTimeout
    {
        get => DecodeDuration(Socket.GetOption(SocketOptions.SndTimeo));
        set => Socket.SetOption(SocketOptions.SndTimeo,
            EncodeDuration(value, nameof(value)));
    }

    /// <summary>
    /// Connects to the endpoint.
    /// </summary>
    public TimeSpan? ConnectTimeout
    {
        get => DecodeDuration(Socket.GetOption(SocketOptions.ConnectTimeout));
        set => Socket.SetOption(SocketOptions.ConnectTimeout,
            EncodeDuration(value, nameof(value)));
    }

    internal TimeSpan? HandshakeInterval
    {
        get => DecodeDuration(Socket.GetOption(SocketOptions.HandshakeIvl));
        set => Socket.SetOption(SocketOptions.HandshakeIvl,
            EncodeDuration(value, nameof(value)));
    }

    /// <summary>
    /// Gets the tcp keep alive.
    /// </summary>
    public int TcpKeepAlive
    {
        get => Socket.GetOption(SocketOptions.TcpKeepalive);
        set => Socket.SetOption(SocketOptions.TcpKeepalive, value);
    }

    internal int TcpKeepAliveCount
    {
        get => Socket.GetOption(SocketOptions.TcpKeepaliveCnt);
        set => Socket.SetOption(SocketOptions.TcpKeepaliveCnt, value);
    }

    internal int TcpKeepAliveIdleSeconds
    {
        get => Socket.GetOption(SocketOptions.TcpKeepaliveIdle);
        set => Socket.SetOption(SocketOptions.TcpKeepaliveIdle, value);
    }

    internal int TcpKeepAliveIntervalSeconds
    {
        get => Socket.GetOption(SocketOptions.TcpKeepaliveIntvl);
        set => Socket.SetOption(SocketOptions.TcpKeepaliveIntvl, value);
    }

    internal int TcpMaxRetransmitTimeout
    {
        get => Socket.GetOption(SocketOptions.TcpMaxRt);
        set => Socket.SetOption(SocketOptions.TcpMaxRt, value);
    }

    /// <summary>
    /// Gets the heartbeat interval.
    /// </summary>
    public TimeSpan? HeartbeatInterval
    {
        get => DecodeDuration(Socket.GetOption(SocketOptions.HeartbeatIvl));
        set => Socket.SetOption(SocketOptions.HeartbeatIvl,
            EncodeDuration(value, nameof(value)));
    }

    /// <summary>
    /// Gets the heartbeat ttl.
    /// </summary>
    public TimeSpan? HeartbeatTtl
    {
        get => DecodeDuration(Socket.GetOption(SocketOptions.HeartbeatTtl));
        set => Socket.SetOption(SocketOptions.HeartbeatTtl,
            EncodeDuration(value, nameof(value)));
    }

    /// <summary>
    /// Gets the heartbeat timeout.
    /// </summary>
    public TimeSpan? HeartbeatTimeout
    {
        get => DecodeDuration(Socket.GetOption(SocketOptions.HeartbeatTimeout));
        set => Socket.SetOption(SocketOptions.HeartbeatTimeout,
            EncodeDuration(value, nameof(value)));
    }

    /// <summary>
    /// Gets the ipv6.
    /// </summary>
    public bool IPv6
    {
        get => Socket.GetOption(SocketOptions.Ipv6) != 0;
        set => Socket.SetOption(SocketOptions.Ipv6, value ? 1 : 0);
    }

    /// <summary>
    /// Gets the tcp no delay.
    /// </summary>
    public bool TcpNoDelay
    {
        get => Socket.GetOption(SocketOptions.TcpNoDelay) != 0;
        set => Socket.SetOption(SocketOptions.TcpNoDelay, value ? 1 : 0);
    }

    internal int TypeOfService
    {
        get => Socket.GetOption(SocketOptions.Tos);
        set => Socket.SetOption(SocketOptions.Tos, value);
    }

    internal int MulticastMaxTransportDataUnit
    {
        get => Socket.GetOption(SocketOptions.MulticastMaxTpdu);
        set => Socket.SetOption(SocketOptions.MulticastMaxTpdu, value);
    }

    internal string BindToDevice
    {
        get => Socket.GetOption(SocketOptions.BindToDevice);
        set => Socket.SetOption(SocketOptions.BindToDevice, value);
    }

    /// <summary>
    /// Gets the immediate.
    /// </summary>
    public bool Immediate
    {
        get => Socket.GetOption(SocketOptions.Immediate) != 0;
        set => Socket.SetOption(SocketOptions.Immediate, value ? 1 : 0);
    }

    internal bool Conflate
    {
        get => Socket.GetOption(SocketOptions.Conflate) != 0;
        set => Socket.SetOption(SocketOptions.Conflate, value ? 1 : 0);
    }

    internal bool Blocky
    {
        get => Socket.GetOption(SocketOptions.Blocky) != 0;
        set => Socket.SetOption(SocketOptions.Blocky, value ? 1 : 0);
    }

    internal bool InvertMatching
    {
        get => Socket.GetOption(SocketOptions.InvertMatching) != 0;
        set => Socket.SetOption(SocketOptions.InvertMatching, value ? 1 : 0);
    }

    /// <summary>
    /// Gets the submit retry mode.
    /// </summary>
    public SubmitRetryMode SubmitRetryMode
    {
        get => (SubmitRetryMode)Socket.GetOption(SocketOptions.SubmitRetryMode);
        set => Socket.SetOption(SocketOptions.SubmitRetryMode, (int)value);
    }

    /// <summary>
    /// Gets the submit retry timeout milliseconds.
    /// </summary>
    public int SubmitRetryTimeoutMilliseconds
    {
        get => Socket.GetOption(SocketOptions.SubmitRetryTimeout);
        set => Socket.SetOption(SocketOptions.SubmitRetryTimeout, value);
    }

    /// <summary>
    /// Gets the submit retry attempts.
    /// </summary>
    public int SubmitRetryAttempts
    {
        get => Socket.GetOption(SocketOptions.SubmitRetryAttempts);
        set => Socket.SetOption(SocketOptions.SubmitRetryAttempts, value);
    }

    internal bool ZmpMetadata
    {
        get => Socket.GetOption(SocketOptions.ZmpMetadata) != 0;
        set => Socket.SetOption(SocketOptions.ZmpMetadata, value ? 1 : 0);
    }

    internal string TlsCertificatePath
    {
        get => Socket.GetOption(SocketOptions.TlsCert);
        set => Socket.SetOption(SocketOptions.TlsCert, value);
    }

    internal string TlsKeyPath
    {
        get => Socket.GetOption(SocketOptions.TlsKey);
        set => Socket.SetOption(SocketOptions.TlsKey, value);
    }

    internal string TlsCaCertificatePath
    {
        get => Socket.GetOption(SocketOptions.TlsCa);
        set => Socket.SetOption(SocketOptions.TlsCa, value);
    }

    internal bool TlsVerify
    {
        get => Socket.GetOption(SocketOptions.TlsVerify) != 0;
        set => Socket.SetOption(SocketOptions.TlsVerify, value ? 1 : 0);
    }

    internal bool TlsRequireClientCertificate
    {
        get => Socket.GetOption(SocketOptions.TlsRequireClientCert) != 0;
        set => Socket.SetOption(SocketOptions.TlsRequireClientCert,
            value ? 1 : 0);
    }

    internal string TlsHostname
    {
        get => Socket.GetOption(SocketOptions.TlsHostname);
        set => Socket.SetOption(SocketOptions.TlsHostname, value);
    }

    internal bool TlsTrustSystem
    {
        get => Socket.GetOption(SocketOptions.TlsTrustSystem) != 0;
        set => Socket.SetOption(SocketOptions.TlsTrustSystem, value ? 1 : 0);
    }

    internal string TlsPassword
    {
        get => Socket.GetOption(SocketOptions.TlsPassword);
        set => Socket.SetOption(SocketOptions.TlsPassword, value);
    }

    /// <summary>
    /// Gets the routing id duplicate policy.
    /// </summary>
    public RidDuplicatePolicy RoutingIdDuplicatePolicy
    {
        get => (RidDuplicatePolicy)Socket.GetOption(
            SocketOptions.RidDuplicatePolicy);
        set => Socket.SetOption(SocketOptions.RidDuplicatePolicy, (int)value);
    }

    /// <summary>
    /// Gets the option.
    /// </summary>
    public string LastEndpoint => Socket.GetOption(SocketOptions.LastEndpoint);

    internal int FileDescriptor => Socket.GetOption(SocketOptions.Fd);

    internal SocketType SocketType => Socket.SocketType;

    internal PollEventFlags Events => (PollEventFlags)Socket.GetOption(SocketOptions.Events);

    internal static TimeSpan? DecodeDuration(int millis)
    {
        return millis < 0 ? null : TimeSpan.FromMilliseconds(millis);
    }

    internal static int EncodeDuration(TimeSpan? duration, string paramName)
    {
        if (duration is null)
            return -1;

        double millis = duration.Value.TotalMilliseconds;
        if (double.IsNaN(millis) || double.IsInfinity(millis)
            || millis < 0 || millis > int.MaxValue)
        {
            throw new ArgumentOutOfRangeException(paramName);
        }

        return (int)Math.Ceiling(millis);
    }

    internal static int EncodePeerWeight(int value, string paramName)
    {
        if (value < 0 || value > 100)
            throw new ArgumentOutOfRangeException(paramName);
        return value;
    }
}

internal interface ISocketOptionEndpoint
{
    void SetOption(SocketOptionKey<int> option, int value);
    void SetOption(SocketOptionKey<long> option, long value);
    void SetOption(SocketOptionKey<ulong> option, ulong value);
    void SetOption(SocketOptionKey<byte[]> option, byte[] value);
    void SetOption(SocketOptionKey<byte[]> option, ReadOnlySpan<byte> value);
    void SetOption(SocketOptionKey<string> option, string value);
    int GetOption(SocketOptionKey<int> option);
    long GetOption(SocketOptionKey<long> option);
    ulong GetOption(SocketOptionKey<ulong> option);
    byte[] GetOption(SocketOptionKey<byte[]> option, int initialSize = 256);
    int GetOption(SocketOptionKey<byte[]> option, Span<byte> destination);
    string GetOption(SocketOptionKey<string> option, int initialSize = 256);
    SocketType SocketType { get; }
}
