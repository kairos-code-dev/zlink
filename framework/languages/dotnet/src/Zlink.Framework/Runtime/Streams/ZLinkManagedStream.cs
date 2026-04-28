using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Backend;
using System.Threading;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkManagedStream : IZLinkStream
{
    private readonly IZLinkBackendStreamSocket _socket;
    private readonly global::Zlink.RoutingId _routingId;

    public ZLinkManagedStream(
        IZLinkBackendStreamSocket socket,
        global::Zlink.RoutingId routingId)
    {
        _socket = socket;
        _routingId = routingId;
        SessionId = _routingId.ToHex();
    }

    public string SessionId { get; }

    public global::Zlink.RoutingId? RoutingId => _routingId;

    public string? LocalAddr { get; private set; }

    public string? RemoteAddr { get; private set; }

    public bool Write(
        global::Zlink.Message payload,
        global::Zlink.SendFlags flags = global::Zlink.SendFlags.None)
    {
        return _socket.Send(_routingId, payload, flags);
    }

    public bool Write(
        global::Zlink.Message header,
        global::Zlink.Message body,
        global::Zlink.SendFlags flags = global::Zlink.SendFlags.None)
    {
        return _socket.Send(_routingId, [header, body], flags);
    }

    public void UpdateAddresses(string? localAddr, string? remoteAddr)
    {
        LocalAddr = localAddr;
        RemoteAddr = remoteAddr;
    }
}
