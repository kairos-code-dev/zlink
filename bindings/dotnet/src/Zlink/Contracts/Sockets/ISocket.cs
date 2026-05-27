// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

public interface ISocket : IZlinkSocket, IDisposable, IAsyncDisposable
{
    CommonSocketOptions Options { get; }

    void Bind(string address);

    void Unbind(string address);

    ISocketMonitor MonitorOpen(SocketEvent events = SocketEvent.All);

    void SetChannelName(string channelName);

    void SetTlsServer(string certPath, string keyPath,
        bool requireClientCert = false);

    void SetTlsClient(string caCertPath, string hostname,
        bool trustSystem = false);

    void Close();
}

public interface IConnectableSocket : ISocket
{
    void Connect(string address);

    void Disconnect(string address);

    void DisconnectRid(RoutingId peerRid);
}
