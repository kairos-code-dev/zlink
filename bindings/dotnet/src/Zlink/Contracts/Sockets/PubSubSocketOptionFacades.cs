// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

/// <summary>
/// Represents stream socket options.
/// </summary>
public sealed class StreamSocketOptions : CommonSocketOptions
{
    internal StreamSocketOptions(ISocketOptionEndpoint socket)
        : base(socket)
    {
    }

    /// <summary>
    /// Gets or sets the notify.
    /// </summary>
    public bool Notify
    {
        get => Socket.GetOption(SocketOptions.StreamNotify) != 0;
        set => Socket.SetOption(SocketOptions.StreamNotify, value ? 1 : 0);
    }
}

/// <summary>
/// Represents pub socket options.
/// </summary>
public sealed class PubSocketOptions : CommonSocketOptions
{
    internal PubSocketOptions(ISocketOptionEndpoint socket)
        : base(socket)
    {
    }

    /// <summary>
    /// Gets or sets the verbose.
    /// </summary>
    public bool Verbose
    {
        get => Socket.GetOption(SocketOptions.XPubVerbose) != 0;
        set => Socket.SetOption(SocketOptions.XPubVerbose, value ? 1 : 0);
    }

    /// <summary>
    /// Gets or sets the verboser.
    /// </summary>
    public bool Verboser
    {
        get => Socket.GetOption(SocketOptions.XPubVerboser) != 0;
        set => Socket.SetOption(SocketOptions.XPubVerboser, value ? 1 : 0);
    }

    /// <summary>
    /// Gets or sets the manual.
    /// </summary>
    public bool Manual
    {
        get => Socket.GetOption(SocketOptions.XPubManual) != 0;
        set => Socket.SetOption(SocketOptions.XPubManual, value ? 1 : 0);
    }

    /// <summary>
    /// Gets or sets the manual last value.
    /// </summary>
    public bool ManualLastValue
    {
        get => Socket.GetOption(SocketOptions.XPubManualLastValue) != 0;
        set => Socket.SetOption(SocketOptions.XPubManualLastValue,
            value ? 1 : 0);
    }

    /// <summary>
    /// Gets or sets the no drop.
    /// </summary>
    public bool NoDrop
    {
        get => Socket.GetOption(SocketOptions.XPubNoDrop) != 0;
        set => Socket.SetOption(SocketOptions.XPubNoDrop, value ? 1 : 0);
    }

    /// <summary>
    /// Gets or sets the welcome message.
    /// </summary>
    public Message WelcomeMessage
    {
        get => Message.From(Socket.GetOption(SocketOptions.XPubWelcomeMsg));
        set => Socket.SetOption(SocketOptions.XPubWelcomeMsg,
            value?.GetString() ?? throw new ArgumentNullException(nameof(value)));
    }

    /// <summary>
    /// Gets or sets the topics count.
    /// </summary>
    public int TopicsCount => Socket.GetOption(SocketOptions.TopicsCount);

    /// <summary>
    /// Approves a pending subscription request.
    /// </summary>
    public void ApproveSubscribe(RoutingId routingId)
    {
        Socket.SetOption(SocketOptions.XPubApproveSubscribe, routingId.ToHex());
    }

    /// <summary>
    /// Rejects a pending subscription request.
    /// </summary>
    public void RejectSubscribe(RoutingId routingId)
    {
        Socket.SetOption(SocketOptions.XPubRejectSubscribe, routingId.ToHex());
    }
}

/// <summary>
/// Represents sub socket options.
/// </summary>
public sealed class SubSocketOptions : CommonSocketOptions
{
    internal SubSocketOptions(ISocketOptionEndpoint socket)
        : base(socket)
    {
    }

    /// <summary>
    /// Gets or sets the topics count.
    /// </summary>
    public int TopicsCount => Socket.GetOption(SocketOptions.SubTopicsCount);
}
