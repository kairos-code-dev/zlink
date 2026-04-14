// SPDX-License-Identifier: MPL-2.0

using System;

namespace Zlink;

public sealed class CommonSocketOptions
{
    private readonly SocketBase _socket;

    internal CommonSocketOptions(SocketBase socket)
    {
        _socket = socket;
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

    public bool Immediate
    {
        get => _socket.GetOption(SocketOptions.Immediate) != 0;
        set => _socket.SetOption(SocketOptions.Immediate, value ? 1 : 0);
    }

    public string LastEndpoint => _socket.GetOption(SocketOptions.LastEndpoint);

    public int FileDescriptor => _socket.GetOption(SocketOptions.Fd);

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
        get => _socket.GetOption(SocketOptions.RouterHandover) != 0;
        set => _socket.SetOption(SocketOptions.RouterHandover, value ? 1 : 0);
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
        set => _node.SetOption(SpotNodeSocketRole.Pub, SocketOptions.SndHwm,
            value);
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
        set => _node.SetOption(SpotNodeSocketRole.Sub, SocketOptions.RcvHwm,
            value);
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
}
