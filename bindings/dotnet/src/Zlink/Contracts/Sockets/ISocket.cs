// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

/// <summary>
/// Marker interface for zlink endpoints that can participate in poll/proxy APIs.
/// </summary>
public interface IZlinkSocket
{
}

/// <summary>
/// Defines the socket contract.
/// </summary>
public interface ISocket : IZlinkSocket, IDisposable, IAsyncDisposable
{
    /// <summary>
    /// Gets the options.
    /// </summary>
    CommonSocketOptions Options { get; }

    /// <summary>
    /// Binds the endpoint.
    /// </summary>
    void Bind(string address);

    /// <summary>
    /// Unbinds the endpoint.
    /// </summary>
    void Unbind(string address);

    /// <summary>
    /// Opens a socket monitor.
    /// </summary>
    ISocketMonitor MonitorOpen(SocketEvent events = SocketEvent.All);

    /// <summary>
    /// Sets the channel name.
    /// </summary>
    void SetChannelName(string channelName);

    /// <summary>
    /// Sets the tls server.
    /// </summary>
    void SetTlsServer(string certPath, string keyPath,
        bool requireClientCert = false);

    /// <summary>
    /// Sets the tls client.
    /// </summary>
    void SetTlsClient(string caCertPath, string hostname,
        bool trustSystem = false);

    /// <summary>
    /// Closes the resource.
    /// </summary>
    void Close();
}

/// <summary>
/// Defines the connectable socket contract.
/// </summary>
public interface IConnectableSocket : ISocket
{
    /// <summary>
    /// Connects to the endpoint.
    /// </summary>
    void Connect(string address);

    /// <summary>
    /// Disconnects from the endpoint.
    /// </summary>
    void Disconnect(string address);

    /// <summary>
    /// Disconnects from the endpoint.
    /// </summary>
    void DisconnectRid(RoutingId peerRid);
}
