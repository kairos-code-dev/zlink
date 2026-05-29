// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

public sealed class StreamSocketOptions : CommonSocketOptions
{
    internal StreamSocketOptions(ISocketOptionEndpoint socket)
        : base(socket)
    {
    }

    public bool Notify
    {
        get => Socket.GetOption(SocketOptions.StreamNotify) != 0;
        set => Socket.SetOption(SocketOptions.StreamNotify, value ? 1 : 0);
    }
}

public sealed class PubSocketOptions : CommonSocketOptions
{
    internal PubSocketOptions(ISocketOptionEndpoint socket)
        : base(socket)
    {
    }

    public bool Verbose
    {
        get => Socket.GetOption(SocketOptions.XPubVerbose) != 0;
        set => Socket.SetOption(SocketOptions.XPubVerbose, value ? 1 : 0);
    }

    public bool Verboser
    {
        get => Socket.GetOption(SocketOptions.XPubVerboser) != 0;
        set => Socket.SetOption(SocketOptions.XPubVerboser, value ? 1 : 0);
    }

    public bool Manual
    {
        get => Socket.GetOption(SocketOptions.XPubManual) != 0;
        set => Socket.SetOption(SocketOptions.XPubManual, value ? 1 : 0);
    }

    public bool ManualLastValue
    {
        get => Socket.GetOption(SocketOptions.XPubManualLastValue) != 0;
        set => Socket.SetOption(SocketOptions.XPubManualLastValue,
            value ? 1 : 0);
    }

    public bool NoDrop
    {
        get => Socket.GetOption(SocketOptions.XPubNoDrop) != 0;
        set => Socket.SetOption(SocketOptions.XPubNoDrop, value ? 1 : 0);
    }

    public Message WelcomeMessage
    {
        get => Message.From(Socket.GetOption(SocketOptions.XPubWelcomeMsg));
        set => Socket.SetOption(SocketOptions.XPubWelcomeMsg,
            value?.GetString() ?? throw new ArgumentNullException(nameof(value)));
    }

    public int TopicsCount => Socket.GetOption(SocketOptions.TopicsCount);

    public void ApproveSubscribe(RoutingId routingId)
    {
        Socket.SetOption(SocketOptions.XPubApproveSubscribe, routingId.ToHex());
    }

    public void RejectSubscribe(RoutingId routingId)
    {
        Socket.SetOption(SocketOptions.XPubRejectSubscribe, routingId.ToHex());
    }
}

public sealed class SubSocketOptions : CommonSocketOptions
{
    internal SubSocketOptions(ISocketOptionEndpoint socket)
        : base(socket)
    {
    }

    public int TopicsCount => Socket.GetOption(SocketOptions.SubTopicsCount);
}
