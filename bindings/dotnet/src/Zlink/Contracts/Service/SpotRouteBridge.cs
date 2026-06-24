// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;

namespace Systems.Zlink;

/// <summary>
/// Capabilities allowed on a SPOT route bridge endpoint.
/// </summary>
[Flags]
public enum SpotRouteBridgeEndpointCapabilities : uint
{
    /// <summary>No SPOT route bridge traffic is allowed.</summary>
    None = 0,
    /// <summary>The endpoint can send SPOT route relay packets.</summary>
    SpotRoute = 0x00000001u,
    /// <summary>The endpoint can send only SPOT route relay packets.</summary>
    RouteOnly = SpotRoute
}

/// <summary>
/// Options used when creating a SPOT route bridge.
/// </summary>
public sealed class SpotRouteBridgeOptions
{
    /// <summary>
    /// Timeout used by requests when a call does not provide one.
    /// </summary>
    public TimeSpan DefaultRequestTimeout { get; set; } = TimeSpan.Zero;
    /// <summary>
    /// Native error reply policy value.
    /// </summary>
    public int ErrorReplyPolicy { get; set; }
    /// <summary>
    /// Native receive mode value.
    /// </summary>
    public int ReceiveMode { get; set; }
}

/// <summary>
/// Options applied to one bridge endpoint.
/// </summary>
public sealed class SpotRouteBridgeEndpointOptions
{
    /// <summary>
    /// Capabilities enabled for this borrowed channel socket.
    /// </summary>
    public SpotRouteBridgeEndpointCapabilities Capabilities { get; set; } =
        SpotRouteBridgeEndpointCapabilities.RouteOnly;
    /// <summary>
    /// Native inbound relay policy value.
    /// </summary>
    public int InboundRelayPolicy { get; set; }
}

/// <summary>
/// Bridges caller-owned channel sockets to a local SPOT node route plane.
/// </summary>
public interface ISpotRouteBridge : IDisposable, IAsyncDisposable
{
    /// <summary>Attaches a caller-owned ROUTER channel socket.</summary>
    void AttachRouterChannel(string channelName, IRouterSocket router,
        SpotRouteBridgeEndpointOptions? options = null);
    /// <summary>Sends a SPOT route relay through an attached channel.</summary>
    bool Send(string channelName, RoutingId targetNodeRid,
        RoutingId targetSpotRid, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None);
    /// <summary>Starts a SPOT route request through an attached channel.</summary>
    bool Request(string channelName, RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags = SendFlags.None,
        TimeSpan timeout = default);
    /// <summary>Lets the bridge process a ROUTER request packet with request metadata.</summary>
    bool HandleRouterReceived(string channelName, RoutingId sourceNodeRid,
        ulong requestSeq, IReadOnlyList<Message> parts);
    /// <summary>Drains pending bridge reply completions.</summary>
    void Drain();
    /// <summary>Closes the bridge without closing borrowed channel sockets.</summary>
    void Close();
}

/// <summary>
/// Publishes into the local SPOT node topic plane without exposing a raw PUB socket.
/// </summary>
public interface ISpotNodePublisher : IDisposable, IAsyncDisposable
{
    /// <summary>Publishes one multipart message to a local SPOT topic.</summary>
    bool Publish(string topic, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None);
    /// <summary>Closes the publisher handle.</summary>
    void Close();
}
