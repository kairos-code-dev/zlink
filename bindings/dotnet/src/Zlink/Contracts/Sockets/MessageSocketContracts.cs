// SPDX-License-Identifier: MPL-2.0

using System;
namespace Systems.Zlink;

/// <summary>
/// Socket contract for message-oriented sockets.
/// </summary>
public interface IMessageSocket : IConnectableSocket
{
    /// <summary>
    /// Start a multipart send operation.
    /// </summary>
    SendOperation Send();

    /// <summary>
    /// Receive a message into caller-provided envelope storage.
    /// </summary>
    /// <param name="result">Reusable storage overwritten on success.</param>
    /// <param name="flags">Receive behavior flags.</param>
    /// <returns>
    /// true on success; false when <see cref="RecvFlags.DontWait"/> is set
    /// and no message is available.
    /// </returns>
    bool Recv(Received result, RecvFlags flags = RecvFlags.None);

    /// <summary>
    /// Register a callback invoked when the socket can accept more sends.
    /// </summary>
    void OnSendReady(Action handler);
}

/// <summary>
/// PAIR socket contract.
/// </summary>
public interface IPairSocket : IMessageSocket
{
}

/// <summary>
/// DEALER socket contract.
/// </summary>
public interface IDealerSocket : IMessageSocket
{
    /// <summary>
    /// Gets DEALER-specific socket options.
    /// </summary>
    new DealerSocketOptions Options { get; }

    /// <summary>
    /// Set the routing id used by this DEALER socket.
    /// </summary>
    void SetRoutingId(RoutingId routingId);

    /// <summary>
    /// Get the routing id used by this DEALER socket.
    /// </summary>
    RoutingId GetRoutingId();

    /// <summary>
    /// Attach a discovery service used by this socket for route-aware operations.
    /// </summary>
    void AttachDiscovery(IDiscovery discovery);

    /// <summary>
    /// Set the logical channel name used by this socket.
    /// </summary>
    new void SetChannelName(string channelName);

    /// <summary>
    /// Get the logical channel name used by this socket.
    /// </summary>
    string GetChannelName();

    /// <summary>
    /// Start a request operation.
    /// </summary>
    RequestOperation Request();
}
