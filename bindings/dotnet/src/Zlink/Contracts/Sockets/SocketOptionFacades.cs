// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

/// <summary>
/// Typed facade over the socket options shared by every socket type.
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
    /// Gets or sets the maximum inbound message size in bytes; -1 means no
    /// limit.
    /// </summary>
    public long MaxMessageSize
    {
        get => Socket.GetOption(SocketOptions.MaxMsgSize);
        set => Socket.SetOption(SocketOptions.MaxMsgSize, value);
    }

    /// <summary>
    /// Gets or sets the maximum number of outbound messages queued before the
    /// socket applies back-pressure; 0 means no limit.
    /// </summary>
    public int SendHighWaterMark
    {
        get => Socket.GetOption(SocketOptions.SndHwm);
        set => Socket.SetOption(SocketOptions.SndHwm, value);
    }

    /// <summary>
    /// Gets or sets the maximum number of inbound messages queued before the
    /// socket applies back-pressure; 0 means no limit.
    /// </summary>
    public int ReceiveHighWaterMark
    {
        get => Socket.GetOption(SocketOptions.RcvHwm);
        set => Socket.SetOption(SocketOptions.RcvHwm, value);
    }

    /// <summary>
    /// Gets or sets the underlying OS send buffer size in bytes; 0 uses the OS
    /// default.
    /// </summary>
    public int SendBufferSize
    {
        get => Socket.GetOption(SocketOptions.SndBuf);
        set => Socket.SetOption(SocketOptions.SndBuf, value);
    }

    /// <summary>
    /// Gets or sets the underlying OS receive buffer size in bytes; 0 uses the
    /// OS default.
    /// </summary>
    public int ReceiveBufferSize
    {
        get => Socket.GetOption(SocketOptions.RcvBuf);
        set => Socket.SetOption(SocketOptions.RcvBuf, value);
    }

    /// <summary>
    /// Gets or sets how long close waits to deliver still-queued messages;
    /// null waits indefinitely.
    /// </summary>
    public TimeSpan? Linger
    {
        get => DecodeDuration(Socket.GetOption(SocketOptions.Linger));
        set => Socket.SetOption(SocketOptions.Linger,
            EncodeDuration(value, nameof(value)));
    }

    /// <summary>
    /// Gets or sets the base delay before reconnecting a broken connection;
    /// null disables reconnection.
    /// </summary>
    public TimeSpan? ReconnectInterval
    {
        get => DecodeDuration(Socket.GetOption(SocketOptions.ReconnectIvl));
        set => Socket.SetOption(SocketOptions.ReconnectIvl,
            EncodeDuration(value, nameof(value)));
    }

    /// <summary>
    /// Gets or sets the maximum delay between reconnection attempts when the
    /// reconnect interval backs off exponentially; null leaves it uncapped.
    /// </summary>
    public TimeSpan? ReconnectIntervalMax
    {
        get => DecodeDuration(Socket.GetOption(SocketOptions.ReconnectIvlMax));
        set => Socket.SetOption(SocketOptions.ReconnectIvlMax,
            EncodeDuration(value, nameof(value)));
    }

    /// <summary>
    /// Gets or sets the maximum length of the queue of pending inbound
    /// connections on a bound endpoint.
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
    /// Gets or sets how long a blocking receive waits for a message before
    /// failing; null blocks indefinitely.
    /// </summary>
    public TimeSpan? ReceiveTimeout
    {
        get => DecodeDuration(Socket.GetOption(SocketOptions.RcvTimeo));
        set => Socket.SetOption(SocketOptions.RcvTimeo,
            EncodeDuration(value, nameof(value)));
    }

    /// <summary>
    /// Gets or sets how long a blocking send waits to enqueue a message before
    /// failing; null blocks indefinitely.
    /// </summary>
    public TimeSpan? SendTimeout
    {
        get => DecodeDuration(Socket.GetOption(SocketOptions.SndTimeo));
        set => Socket.SetOption(SocketOptions.SndTimeo,
            EncodeDuration(value, nameof(value)));
    }

    /// <summary>
    /// Gets or sets the time limit for a single connection attempt; null uses
    /// the OS default.
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
    /// Gets or sets TCP keep-alive: -1 uses the OS default, 0 disables it, 1
    /// enables it.
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
    /// Gets or sets the interval between heartbeat pings on an idle connection;
    /// null disables heartbeats.
    /// </summary>
    public TimeSpan? HeartbeatInterval
    {
        get => DecodeDuration(Socket.GetOption(SocketOptions.HeartbeatIvl));
        set => Socket.SetOption(SocketOptions.HeartbeatIvl,
            EncodeDuration(value, nameof(value)));
    }

    /// <summary>
    /// Gets or sets how long the remote peer should consider this connection
    /// alive without a heartbeat; null leaves it unset.
    /// </summary>
    public TimeSpan? HeartbeatTtl
    {
        get => DecodeDuration(Socket.GetOption(SocketOptions.HeartbeatTtl));
        set => Socket.SetOption(SocketOptions.HeartbeatTtl,
            EncodeDuration(value, nameof(value)));
    }

    /// <summary>
    /// Gets or sets how long to wait for a heartbeat reply before treating the
    /// connection as dead; null falls back to the heartbeat interval.
    /// </summary>
    public TimeSpan? HeartbeatTimeout
    {
        get => DecodeDuration(Socket.GetOption(SocketOptions.HeartbeatTimeout));
        set => Socket.SetOption(SocketOptions.HeartbeatTimeout,
            EncodeDuration(value, nameof(value)));
    }

    /// <summary>
    /// Gets or sets whether IPv6 connections are enabled.
    /// </summary>
    public bool IPv6
    {
        get => Socket.GetOption(SocketOptions.Ipv6) != 0;
        set => Socket.SetOption(SocketOptions.Ipv6, value ? 1 : 0);
    }

    /// <summary>
    /// Gets or sets whether Nagle's algorithm is disabled (TCP_NODELAY) so
    /// small messages are sent without buffering delay.
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
    /// Gets or sets whether messages are queued only to fully established
    /// connections; when false they may also queue to pending connections.
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
    /// Gets or sets whether a submit that hits a local failure (such as
    /// back-pressure) is retried; see <see cref="Systems.Zlink.SubmitRetryMode"/>.
    /// </summary>
    public SubmitRetryMode SubmitRetryMode
    {
        get => (SubmitRetryMode)Socket.GetOption(SocketOptions.SubmitRetryMode);
        set => Socket.SetOption(SocketOptions.SubmitRetryMode, (int)value);
    }

    /// <summary>
    /// Gets or sets the total time budget, in milliseconds, for submit retries
    /// when <see cref="SubmitRetryMode"/> is enabled.
    /// </summary>
    public int SubmitRetryTimeoutMilliseconds
    {
        get => Socket.GetOption(SocketOptions.SubmitRetryTimeout);
        set => Socket.SetOption(SocketOptions.SubmitRetryTimeout, value);
    }

    /// <summary>
    /// Gets or sets the maximum number of submit retry attempts when
    /// <see cref="SubmitRetryMode"/> is enabled.
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
    /// Gets or sets how the socket reacts when a connecting peer presents a
    /// routing id already in use; see <see cref="RidDuplicatePolicy"/>.
    /// </summary>
    public RidDuplicatePolicy RoutingIdDuplicatePolicy
    {
        get => (RidDuplicatePolicy)Socket.GetOption(
            SocketOptions.RidDuplicatePolicy);
        set => Socket.SetOption(SocketOptions.RidDuplicatePolicy, (int)value);
    }

    /// <summary>
    /// Gets the concrete endpoint the socket last bound to, for example the
    /// resolved address and port after binding to a wildcard.
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
