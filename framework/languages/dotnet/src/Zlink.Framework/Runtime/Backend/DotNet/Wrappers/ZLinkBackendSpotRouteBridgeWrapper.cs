namespace Zlink.Framework.Runtime.Backend.DotNet.Wrappers;

internal sealed class ZLinkBackendSpotRouteBridgeWrapper(ISpotRouteBridge nativeBridge) :
    IZLinkBackendSpotRouteBridge
{
    public object NativeInstance => nativeBridge;

    public void AttachRouterChannel(
        string channelName,
        IZLinkBackendRouterSocket router,
        SpotRouteBridgeEndpointOptions? options = null)
    {
        nativeBridge.AttachRouterChannel(
            channelName,
            router.RequireNative<IRouterSocket>(),
            options);
    }

    public bool Send(
        string channelName,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        return nativeBridge.Send(channelName, targetNodeRid, targetSpotRid, parts, flags);
    }

    public bool Request(
        string channelName,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout)
    {
        return nativeBridge.Request(
            channelName,
            targetNodeRid,
            targetSpotRid,
            parts,
            (result, reply) => callback(result, reply),
            flags,
            timeout ?? default);
    }

    public bool HandleRouterReceived(
        string channelName,
        RoutingId sourceNodeRid,
        ulong requestSeq,
        IReadOnlyList<Message> parts)
    {
        return nativeBridge.HandleRouterReceived(
            channelName,
            sourceNodeRid,
            requestSeq,
            parts);
    }

    public void Drain()
    {
        nativeBridge.Drain();
    }

    public ValueTask DisposeAsync()
    {
        return nativeBridge.DisposeAsync();
    }
}