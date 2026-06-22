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
    /// <summary>The endpoint can pass non-bridge channel inbound packets.</summary>
    ChannelInbound = 0x00000002u,
    /// <summary>The endpoint can send only SPOT route relay packets.</summary>
    RouteOnly = SpotRoute,
    /// <summary>The endpoint can send SPOT routes and accept channel inbound packets.</summary>
    RouteWithChannelInbound = SpotRoute | ChannelInbound
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
/// Snapshot of bridge counters and request state.
/// </summary>
public readonly struct SpotRouteBridgeSummary
{
    internal SpotRouteBridgeSummary(uint attachedChannelCount,
        ulong pendingRequestCount, ulong rejectedInboundCount,
        ulong malformedInboundCount, ulong routedSendFailureCount)
    {
        AttachedChannelCount = attachedChannelCount;
        PendingRequestCount = pendingRequestCount;
        RejectedInboundCount = rejectedInboundCount;
        MalformedInboundCount = malformedInboundCount;
        RoutedSendFailureCount = routedSendFailureCount;
    }

    /// <summary>Number of channel endpoints attached to the bridge.</summary>
    public uint AttachedChannelCount { get; }
    /// <summary>Number of pending bridge or channel request replies.</summary>
    public ulong PendingRequestCount { get; }
    /// <summary>Number of inbound packets rejected by policy.</summary>
    public ulong RejectedInboundCount { get; }
    /// <summary>Number of malformed inbound bridge packets.</summary>
    public ulong MalformedInboundCount { get; }
    /// <summary>Number of route sends that failed.</summary>
    public ulong RoutedSendFailureCount { get; }
}

/// <summary>
/// Bridges caller-owned channel sockets to a local SPOT node route plane.
/// </summary>
public interface ISpotRouteBridge : IDisposable, IAsyncDisposable
{
    /// <summary>Attaches a caller-owned DEALER channel socket.</summary>
    void AttachDealerChannel(string channelName, IDealerSocket dealer,
        SpotRouteBridgeEndpointOptions? options = null);
    /// <summary>Attaches a caller-owned ROUTER channel socket.</summary>
    void AttachRouterChannel(string channelName, IRouterSocket router,
        SpotRouteBridgeEndpointOptions? options = null);
    /// <summary>Sets the target SPOT node for a routed channel endpoint.</summary>
    void SetTargetNode(string channelName, RoutingId targetNodeRid);
    /// <summary>Sends a SPOT route relay through an attached channel.</summary>
    bool Send(string channelName, RoutingId targetSpotRid,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None);
    /// <summary>Starts a SPOT route request through an attached channel.</summary>
    bool Request(string channelName, RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags = SendFlags.None,
        TimeSpan timeout = default);
    /// <summary>Lets the bridge process a ROUTER packet already received by the caller.</summary>
    bool HandleRouterReceived(string channelName, RoutingId sourceNodeRid,
        IReadOnlyList<Message> parts);
    /// <summary>Lets the bridge process a ROUTER request packet with request metadata.</summary>
    bool HandleRouterReceived(string channelName, RoutingId sourceNodeRid,
        ulong requestSeq, IReadOnlyList<Message> parts);
    /// <summary>Lets the bridge process a DEALER packet already received by the caller.</summary>
    bool HandleDealerReceived(string channelName, IReadOnlyList<Message> parts);
    /// <summary>Lets the bridge process a DEALER packet with request metadata.</summary>
    bool HandleDealerReceived(string channelName,
        ReceivedMessageType messageType, ulong requestSeq,
        IReadOnlyList<Message> parts);
    /// <summary>Drains pending bridge reply completions.</summary>
    void Drain();
    /// <summary>Returns bridge counters and pending request count.</summary>
    SpotRouteBridgeSummary Summary();
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
