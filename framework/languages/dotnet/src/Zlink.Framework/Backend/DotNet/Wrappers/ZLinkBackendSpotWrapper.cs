namespace Zlink.Framework.Backend.DotNet.Wrappers;


internal sealed class ZLinkBackendSpotWrapper(Spot nativeSpot) : IZLinkBackendSpot
{
    public object NativeInstance => nativeSpot;

    public RoutingId RoutingId => nativeSpot.RoutingId;

    public void SetRoutingId(RoutingId routingId)
    {
        nativeSpot.SetRoutingId(routingId);
    }

    public void SetSubscription(string topic)
    {
        nativeSpot.SetSubscription(topic);
    }

    public TopicMessage? Subscribe(RecvFlags flags)
    {
        return nativeSpot.Subscribe(flags);
    }

    public Received RecvRouted(RecvFlags flags)
    {
        return nativeSpot.RecvRouted(flags);
    }

    public void OnDispatchEvent(Action<ZLinkBackendSpotDispatchInfo> handler)
    {
        nativeSpot.OnDispatchEvent((_, info) => handler(info.ToFramework()));
    }

    public void OnSendReady(Action handler)
    {
        nativeSpot.OnSendReady(handler);
    }

    public void DrainChannelReplyFrom(IntPtr dealerSubject)
    {
        nativeSpot.DrainChannelReplyFrom(dealerSubject);
    }

    public bool RequestChannel(
        string channelName,
        Message message,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags,
        TimeSpan? timeout)
    {
        return nativeSpot.RequestChannel(channelName, message, callback, flags, timeout);
    }

    public bool SendChannel(
        string channelName,
        Message message,
        SendFlags flags)
    {
        return nativeSpot.SendChannel(channelName, message, flags);
    }

    public bool Publish(
        string serviceName,
        string topic,
        Message message,
        SendFlags flags)
    {
        return nativeSpot.Publish(serviceName, topic, message, flags);
    }

    public bool SendToSpot(
        RoutingId targetRid,
        RoutingId spotRid,
        Message message,
        SendFlags flags)
    {
        return nativeSpot.SendToSpot(targetRid, spotRid, message, flags);
    }

    public ValueTask DisposeAsync() => nativeSpot.DisposeAsync();
}
