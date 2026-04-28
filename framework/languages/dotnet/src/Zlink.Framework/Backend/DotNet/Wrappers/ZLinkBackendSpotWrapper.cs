namespace Zlink.Framework.Backend.DotNet.Wrappers;


internal sealed class ZLinkBackendSpotWrapper(global::Zlink.Spot nativeSpot) : IZLinkBackendSpot
{
    public object NativeInstance => nativeSpot;

    public global::Zlink.RoutingId RoutingId => nativeSpot.RoutingId;

    public void SetRoutingId(global::Zlink.RoutingId routingId)
    {
        nativeSpot.SetRoutingId(routingId);
    }

    public void SetSubscription(string topic)
    {
        nativeSpot.SetSubscription(topic);
    }

    public global::Zlink.TopicMessage? Subscribe(global::Zlink.RecvFlags flags)
    {
        return nativeSpot.Subscribe(flags);
    }

    public global::Zlink.Received RecvRouted(global::Zlink.RecvFlags flags)
    {
        return nativeSpot.RecvRouted(flags);
    }

    public void OnDispatchEvent(Action<ZLinkBackendSpotDispatchInfo> handler)
    {
        nativeSpot.OnDispatchEvent((_, info) => handler(info.ToFramework()));
    }

    public void DrainChannelReplyFrom(IntPtr dealerSubject)
    {
        nativeSpot.DrainChannelReplyFrom(dealerSubject);
    }

    public async ValueTask<IReadOnlyList<global::Zlink.Message>> RequestChannelAsync(
        string channelName,
        global::Zlink.Message message,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        return await nativeSpot.RequestChannelAsync(
            channelName,
            message,
            timeout,
            cancellationToken).ConfigureAwait(false);
    }

    public bool SendChannel(
        string channelName,
        global::Zlink.Message message,
        global::Zlink.SendFlags flags)
    {
        return nativeSpot.SendChannel(channelName, message, flags);
    }

    public bool Publish(
        string serviceName,
        string topic,
        global::Zlink.Message message,
        global::Zlink.SendFlags flags)
    {
        return nativeSpot.Publish(serviceName, topic, message, flags);
    }

    public bool SendToSpot(
        global::Zlink.RoutingId targetRid,
        global::Zlink.RoutingId spotRid,
        global::Zlink.Message message,
        global::Zlink.SendFlags flags)
    {
        return nativeSpot.SendToSpot(targetRid, spotRid, message, flags);
    }

    public ValueTask DisposeAsync() => nativeSpot.DisposeAsync();
}
