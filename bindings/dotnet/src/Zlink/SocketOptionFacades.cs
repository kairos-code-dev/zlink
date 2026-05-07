// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

public sealed class CommonSocketOptions
{
    private readonly SocketBase _socket;

    internal CommonSocketOptions(SocketBase socket)
    {
        _socket = socket;
    }

    public ulong Affinity
    {
        get => _socket.GetOption(SocketOptions.Affinity);
        set => _socket.SetOption(SocketOptions.Affinity, value);
    }

    public int Rate
    {
        get => _socket.GetOption(SocketOptions.Rate);
        set => _socket.SetOption(SocketOptions.Rate, value);
    }

    public TimeSpan? RecoveryInterval
    {
        get => DecodeDuration(_socket.GetOption(SocketOptions.RecoveryIvl));
        set => _socket.SetOption(SocketOptions.RecoveryIvl,
            EncodeDuration(value, nameof(value)));
    }

    public long MaxMessageSize
    {
        get => _socket.GetOption(SocketOptions.MaxMsgSize);
        set => _socket.SetOption(SocketOptions.MaxMsgSize, value);
    }

    public int SendHighWaterMark
    {
        get => _socket.GetOption(SocketOptions.SndHwm);
        set => _socket.SetOption(SocketOptions.SndHwm, value);
    }

    public int ReceiveHighWaterMark
    {
        get => _socket.GetOption(SocketOptions.RcvHwm);
        set => _socket.SetOption(SocketOptions.RcvHwm, value);
    }

    public int SendBufferSize
    {
        get => _socket.GetOption(SocketOptions.SndBuf);
        set => _socket.SetOption(SocketOptions.SndBuf, value);
    }

    public int ReceiveBufferSize
    {
        get => _socket.GetOption(SocketOptions.RcvBuf);
        set => _socket.SetOption(SocketOptions.RcvBuf, value);
    }

    public TimeSpan? Linger
    {
        get => DecodeDuration(_socket.GetOption(SocketOptions.Linger));
        set => _socket.SetOption(SocketOptions.Linger,
            EncodeDuration(value, nameof(value)));
    }

    public TimeSpan? ReconnectInterval
    {
        get => DecodeDuration(_socket.GetOption(SocketOptions.ReconnectIvl));
        set => _socket.SetOption(SocketOptions.ReconnectIvl,
            EncodeDuration(value, nameof(value)));
    }

    public TimeSpan? ReconnectIntervalMax
    {
        get => DecodeDuration(_socket.GetOption(SocketOptions.ReconnectIvlMax));
        set => _socket.SetOption(SocketOptions.ReconnectIvlMax,
            EncodeDuration(value, nameof(value)));
    }

    public int Backlog
    {
        get => _socket.GetOption(SocketOptions.Backlog);
        set => _socket.SetOption(SocketOptions.Backlog, value);
    }

    public int MulticastHops
    {
        get => _socket.GetOption(SocketOptions.MulticastHops);
        set => _socket.SetOption(SocketOptions.MulticastHops, value);
    }

    public TimeSpan? ReceiveTimeout
    {
        get => DecodeDuration(_socket.GetOption(SocketOptions.RcvTimeo));
        set => _socket.SetOption(SocketOptions.RcvTimeo,
            EncodeDuration(value, nameof(value)));
    }

    public TimeSpan? SendTimeout
    {
        get => DecodeDuration(_socket.GetOption(SocketOptions.SndTimeo));
        set => _socket.SetOption(SocketOptions.SndTimeo,
            EncodeDuration(value, nameof(value)));
    }

    public TimeSpan? ConnectTimeout
    {
        get => DecodeDuration(_socket.GetOption(SocketOptions.ConnectTimeout));
        set => _socket.SetOption(SocketOptions.ConnectTimeout,
            EncodeDuration(value, nameof(value)));
    }

    public TimeSpan? HandshakeInterval
    {
        get => DecodeDuration(_socket.GetOption(SocketOptions.HandshakeIvl));
        set => _socket.SetOption(SocketOptions.HandshakeIvl,
            EncodeDuration(value, nameof(value)));
    }

    public int TcpKeepAlive
    {
        get => _socket.GetOption(SocketOptions.TcpKeepalive);
        set => _socket.SetOption(SocketOptions.TcpKeepalive, value);
    }

    public int TcpKeepAliveCount
    {
        get => _socket.GetOption(SocketOptions.TcpKeepaliveCnt);
        set => _socket.SetOption(SocketOptions.TcpKeepaliveCnt, value);
    }

    public int TcpKeepAliveIdleSeconds
    {
        get => _socket.GetOption(SocketOptions.TcpKeepaliveIdle);
        set => _socket.SetOption(SocketOptions.TcpKeepaliveIdle, value);
    }

    public int TcpKeepAliveIntervalSeconds
    {
        get => _socket.GetOption(SocketOptions.TcpKeepaliveIntvl);
        set => _socket.SetOption(SocketOptions.TcpKeepaliveIntvl, value);
    }

    public int TcpMaxRetransmitTimeout
    {
        get => _socket.GetOption(SocketOptions.TcpMaxRt);
        set => _socket.SetOption(SocketOptions.TcpMaxRt, value);
    }

    public TimeSpan? HeartbeatInterval
    {
        get => DecodeDuration(_socket.GetOption(SocketOptions.HeartbeatIvl));
        set => _socket.SetOption(SocketOptions.HeartbeatIvl,
            EncodeDuration(value, nameof(value)));
    }

    public TimeSpan? HeartbeatTtl
    {
        get => DecodeDuration(_socket.GetOption(SocketOptions.HeartbeatTtl));
        set => _socket.SetOption(SocketOptions.HeartbeatTtl,
            EncodeDuration(value, nameof(value)));
    }

    public TimeSpan? HeartbeatTimeout
    {
        get => DecodeDuration(_socket.GetOption(SocketOptions.HeartbeatTimeout));
        set => _socket.SetOption(SocketOptions.HeartbeatTimeout,
            EncodeDuration(value, nameof(value)));
    }

    public bool IPv6
    {
        get => _socket.GetOption(SocketOptions.Ipv6) != 0;
        set => _socket.SetOption(SocketOptions.Ipv6, value ? 1 : 0);
    }

    public bool TcpNoDelay
    {
        get => _socket.GetOption(SocketOptions.TcpNoDelay) != 0;
        set => _socket.SetOption(SocketOptions.TcpNoDelay, value ? 1 : 0);
    }

    public int TypeOfService
    {
        get => _socket.GetOption(SocketOptions.Tos);
        set => _socket.SetOption(SocketOptions.Tos, value);
    }

    public int MulticastMaxTransportDataUnit
    {
        get => _socket.GetOption(SocketOptions.MulticastMaxTpdu);
        set => _socket.SetOption(SocketOptions.MulticastMaxTpdu, value);
    }

    public string BindToDevice
    {
        get => _socket.GetOption(SocketOptions.BindToDevice);
        set => _socket.SetOption(SocketOptions.BindToDevice, value);
    }

    public bool Immediate
    {
        get => _socket.GetOption(SocketOptions.Immediate) != 0;
        set => _socket.SetOption(SocketOptions.Immediate, value ? 1 : 0);
    }

    public bool Conflate
    {
        get => _socket.GetOption(SocketOptions.Conflate) != 0;
        set => _socket.SetOption(SocketOptions.Conflate, value ? 1 : 0);
    }

    public bool Blocky
    {
        get => _socket.GetOption(SocketOptions.Blocky) != 0;
        set => _socket.SetOption(SocketOptions.Blocky, value ? 1 : 0);
    }

    public bool InvertMatching
    {
        get => _socket.GetOption(SocketOptions.InvertMatching) != 0;
        set => _socket.SetOption(SocketOptions.InvertMatching, value ? 1 : 0);
    }

    public bool ZmpMetadata
    {
        get => _socket.GetOption(SocketOptions.ZmpMetadata) != 0;
        set => _socket.SetOption(SocketOptions.ZmpMetadata, value ? 1 : 0);
    }

    public string TlsCertificatePath
    {
        get => _socket.GetOption(SocketOptions.TlsCert);
        set => _socket.SetOption(SocketOptions.TlsCert, value);
    }

    public string TlsKeyPath
    {
        get => _socket.GetOption(SocketOptions.TlsKey);
        set => _socket.SetOption(SocketOptions.TlsKey, value);
    }

    public string TlsCaCertificatePath
    {
        get => _socket.GetOption(SocketOptions.TlsCa);
        set => _socket.SetOption(SocketOptions.TlsCa, value);
    }

    public bool TlsVerify
    {
        get => _socket.GetOption(SocketOptions.TlsVerify) != 0;
        set => _socket.SetOption(SocketOptions.TlsVerify, value ? 1 : 0);
    }

    public bool TlsRequireClientCertificate
    {
        get => _socket.GetOption(SocketOptions.TlsRequireClientCert) != 0;
        set => _socket.SetOption(SocketOptions.TlsRequireClientCert,
            value ? 1 : 0);
    }

    public string TlsHostname
    {
        get => _socket.GetOption(SocketOptions.TlsHostname);
        set => _socket.SetOption(SocketOptions.TlsHostname, value);
    }

    public bool TlsTrustSystem
    {
        get => _socket.GetOption(SocketOptions.TlsTrustSystem) != 0;
        set => _socket.SetOption(SocketOptions.TlsTrustSystem, value ? 1 : 0);
    }

    public string TlsPassword
    {
        get => _socket.GetOption(SocketOptions.TlsPassword);
        set => _socket.SetOption(SocketOptions.TlsPassword, value);
    }

    public RidDuplicatePolicy RoutingIdDuplicatePolicy
    {
        get => (RidDuplicatePolicy)_socket.GetOption(
            SocketOptions.RidDuplicatePolicy);
        set => _socket.SetOption(SocketOptions.RidDuplicatePolicy, (int)value);
    }

    public int AutoHwmMessageUnitBytes
    {
        get => _socket.GetOption(SocketOptions.AutoHwmMsgUnitBytes);
        set => _socket.SetOption(SocketOptions.AutoHwmMsgUnitBytes, value);
    }

    public string LastEndpoint => _socket.GetOption(SocketOptions.LastEndpoint);

    public int FileDescriptor => _socket.GetOption(SocketOptions.Fd);

    public SocketType SocketType => _socket.Kernel.Type;

    public PollEvents Events => (PollEvents)_socket.GetOption(SocketOptions.Events);

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
}

public sealed class DealerSocketOptions
{
    private readonly DealerSocket _socket;

    internal DealerSocketOptions(DealerSocket socket)
    {
        _socket = socket;
    }

    public RoutingId RoutingId
    {
        get => RoutingId.FromBytes(_socket.GetOption(SocketOptions.RoutingId));
        set => _socket.SetOption(SocketOptions.RoutingId, value.ToBytes());
    }

    public bool ProbeRouter
    {
        get => _socket.GetOption(SocketOptions.ProbeRouter) != 0;
        set => _socket.SetOption(SocketOptions.ProbeRouter, value ? 1 : 0);
    }

    public TimeSpan? RequestTimeout
    {
        set => _socket.SetOption(SocketOptions.DealerRequestTimeout,
            CommonSocketOptions.EncodeDuration(value, nameof(value)));
    }

    public int Weight
    {
        set => _socket.SetOption(SocketOptions.DealerWeight, value);
    }
}

public sealed class RouterSocketOptions
{
    private readonly RouterSocket _socket;

    internal RouterSocketOptions(RouterSocket socket)
    {
        _socket = socket;
    }

    public RoutingId RoutingId
    {
        get => RoutingId.FromBytes(_socket.GetOption(SocketOptions.RoutingId));
        set => _socket.SetOption(SocketOptions.RoutingId, value.ToBytes());
    }

    public bool Mandatory
    {
        get => _socket.GetOption(SocketOptions.RouterMandatory) != 0;
        set => _socket.SetOption(SocketOptions.RouterMandatory, value ? 1 : 0);
    }

    public bool Handover
    {
        get => _socket.Options.RoutingIdDuplicatePolicy
            == RidDuplicatePolicy.Handover;
        set => _socket.Options.RoutingIdDuplicatePolicy = value
            ? RidDuplicatePolicy.Handover
            : RidDuplicatePolicy.Reject;
    }

    public bool Probe
    {
        get => _socket.GetOption(SocketOptions.ProbeRouter) != 0;
        set => _socket.SetOption(SocketOptions.ProbeRouter, value ? 1 : 0);
    }

    public RoutingId ConnectRoutingId
    {
        get => RoutingId.FromBytes(_socket.GetOption(SocketOptions.ConnectRoutingId));
        set => _socket.SetOption(SocketOptions.ConnectRoutingId, value.ToBytes());
    }

    public TimeSpan? RequestTimeout
    {
        get => CommonSocketOptions.DecodeDuration(
            _socket.GetOption(SocketOptions.RouterRequestTimeout));
        set => _socket.SetOption(SocketOptions.RouterRequestTimeout,
            CommonSocketOptions.EncodeDuration(value, nameof(value)));
    }

    public int Weight
    {
        get => _socket.GetOption(SocketOptions.RouterWeight);
        set => _socket.SetOption(SocketOptions.RouterWeight, value);
    }
}

public sealed class StreamSocketOptions
{
    private readonly StreamSocket _socket;

    internal StreamSocketOptions(StreamSocket socket)
    {
        _socket = socket;
    }

    public bool Notify
    {
        get => _socket.GetOption(SocketOptions.StreamNotify) != 0;
        set => _socket.SetOption(SocketOptions.StreamNotify, value ? 1 : 0);
    }

    public RoutingId ConnectRoutingId
    {
        get => RoutingId.FromBytes(_socket.GetOption(SocketOptions.ConnectRoutingId));
        set => _socket.SetOption(SocketOptions.ConnectRoutingId, value.ToBytes());
    }
}

public sealed class XPubSocketOptions
{
    private readonly XPubSocket _socket;

    internal XPubSocketOptions(XPubSocket socket)
    {
        _socket = socket;
    }

    public bool Verbose
    {
        get => _socket.GetOption(SocketOptions.XPubVerbose) != 0;
        set => _socket.SetOption(SocketOptions.XPubVerbose, value ? 1 : 0);
    }

    public bool Verboser
    {
        get => _socket.GetOption(SocketOptions.XPubVerboser) != 0;
        set => _socket.SetOption(SocketOptions.XPubVerboser, value ? 1 : 0);
    }

    public bool Manual
    {
        get => _socket.GetOption(SocketOptions.XPubManual) != 0;
        set => _socket.SetOption(SocketOptions.XPubManual, value ? 1 : 0);
    }

    public bool ManualLastValue
    {
        get => _socket.GetOption(SocketOptions.XPubManualLastValue) != 0;
        set => _socket.SetOption(SocketOptions.XPubManualLastValue,
            value ? 1 : 0);
    }

    public bool NoDrop
    {
        get => _socket.GetOption(SocketOptions.XPubNoDrop) != 0;
        set => _socket.SetOption(SocketOptions.XPubNoDrop, value ? 1 : 0);
    }

    public string WelcomeMessage
    {
        get => _socket.GetOption(SocketOptions.XPubWelcomeMsg);
        set => _socket.SetOption(SocketOptions.XPubWelcomeMsg, value);
    }

    public int TopicsCount => _socket.GetOption(SocketOptions.TopicsCount);

    public void ApproveSubscribe(string topicOrPattern)
    {
        _socket.SetOption(SocketOptions.XPubApproveSubscribe, topicOrPattern);
    }

    public void RejectSubscribe(string topicOrPattern)
    {
        _socket.SetOption(SocketOptions.XPubRejectSubscribe, topicOrPattern);
    }
}

public sealed class PubSocketOptions
{
    private readonly PubSocket _socket;

    internal PubSocketOptions(PubSocket socket)
    {
        _socket = socket;
    }

    public bool Verbose
    {
        get => _socket.GetOption(SocketOptions.XPubVerbose) != 0;
        set => _socket.SetOption(SocketOptions.XPubVerbose, value ? 1 : 0);
    }

    public bool Verboser
    {
        get => _socket.GetOption(SocketOptions.XPubVerboser) != 0;
        set => _socket.SetOption(SocketOptions.XPubVerboser, value ? 1 : 0);
    }

    public bool Manual
    {
        get => _socket.GetOption(SocketOptions.XPubManual) != 0;
        set => _socket.SetOption(SocketOptions.XPubManual, value ? 1 : 0);
    }

    public bool ManualLastValue
    {
        get => _socket.GetOption(SocketOptions.XPubManualLastValue) != 0;
        set => _socket.SetOption(SocketOptions.XPubManualLastValue,
            value ? 1 : 0);
    }

    public bool NoDrop
    {
        get => _socket.GetOption(SocketOptions.XPubNoDrop) != 0;
        set => _socket.SetOption(SocketOptions.XPubNoDrop, value ? 1 : 0);
    }

    public string WelcomeMessage
    {
        get => _socket.GetOption(SocketOptions.XPubWelcomeMsg);
        set => _socket.SetOption(SocketOptions.XPubWelcomeMsg, value);
    }

    public int TopicsCount => _socket.GetOption(SocketOptions.TopicsCount);

    public void ApproveSubscribe(string topicOrPattern)
    {
        _socket.SetOption(SocketOptions.XPubApproveSubscribe, topicOrPattern);
    }

    public void RejectSubscribe(string topicOrPattern)
    {
        _socket.SetOption(SocketOptions.XPubRejectSubscribe, topicOrPattern);
    }
}

public sealed class SubSocketOptions
{
    private readonly SocketBase _socket;

    internal SubSocketOptions(SocketBase socket)
    {
        _socket = socket;
    }

    public int TopicsCount => _socket.GetOption(SocketOptions.SubTopicsCount);
}

public sealed class SpotNodePublisherOptions
{
    private readonly SpotNode _node;

    internal SpotNodePublisherOptions(SpotNode node)
    {
        _node = node;
    }

    public int SendHighWaterMark
    {
        set => _node.SetPubSubHighWaterMark(value);
    }

    public TimeSpan? SendTimeout
    {
        set => _node.SetOption(SpotNodeSocketRole.Pub, SocketOptions.SndTimeo,
            CommonSocketOptions.EncodeDuration(value, nameof(value)));
    }

    public TimeSpan? Linger
    {
        set => _node.SetOption(SpotNodeSocketRole.Pub, SocketOptions.Linger,
            CommonSocketOptions.EncodeDuration(value, nameof(value)));
    }

    public bool NoDrop
    {
        set => _node.SetOption(SpotNodeSocketRole.Pub, SocketOptions.XPubNoDrop,
            value ? 1 : 0);
    }

    public int AutoHwmMessageUnitBytes
    {
        set => _node.SetOption(SpotNodeSocketRole.Pub,
            SocketOptions.AutoHwmMsgUnitBytes, value);
    }
}

public sealed class SpotNodeSubscriberOptions
{
    private readonly SpotNode _node;

    internal SpotNodeSubscriberOptions(SpotNode node)
    {
        _node = node;
    }

    public int ReceiveHighWaterMark
    {
        set => _node.SetPubSubHighWaterMark(value);
    }

    public TimeSpan? ReceiveTimeout
    {
        set => _node.SetOption(SpotNodeSocketRole.Sub, SocketOptions.RcvTimeo,
            CommonSocketOptions.EncodeDuration(value, nameof(value)));
    }

    public TimeSpan? Linger
    {
        set => _node.SetOption(SpotNodeSocketRole.Sub, SocketOptions.Linger,
            CommonSocketOptions.EncodeDuration(value, nameof(value)));
    }

    public int AutoHwmMessageUnitBytes
    {
        set => _node.SetOption(SpotNodeSocketRole.Sub,
            SocketOptions.AutoHwmMsgUnitBytes, value);
    }
}
