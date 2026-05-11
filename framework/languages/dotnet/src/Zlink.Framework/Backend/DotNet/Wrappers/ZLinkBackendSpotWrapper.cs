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
        return nativeSpot.RecvRouted(flags)
            ?? throw new ZlinkRecvException(ZlinkRecvException.ErrorCode.NoData);
    }

    public void OnDispatchEvent(Action<ZLinkBackendSpotDispatchInfo> handler)
    {
        nativeSpot.OnDispatchEvent(info => handler(info.ToFramework()));
    }

    public void OnSendReady(Action handler)
    {
        nativeSpot.OnSendReady(handler);
    }

    public bool RequestChannel(
        string channelName,
        Message message,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout)
    {
        var operation = nativeSpot.RequestChannel(channelName)
            .Message(message)
            .Flags(flags);
        if (timeout is { } value)
        {
            operation = operation.Timeout(value);
        }

        return operation.Submit(callback);
    }

    public bool SendChannel(
        string channelName,
        Message message,
        SendFlags flags)
    {
        return nativeSpot.SendChannel(channelName)
            .Message(message)
            .Flags(flags)
            .Submit();
    }

    public bool Publish(
        string topic,
        Message message,
        SendFlags flags)
    {
        return nativeSpot.Publish(topic)
            .Message(message)
            .Flags(flags)
            .Submit();
    }

    public bool SendToSpot(
        RoutingId targetRid,
        RoutingId spotRid,
        Message message,
        SendFlags flags)
    {
        return nativeSpot.SendToSpot(targetRid, spotRid)
            .Message(message)
            .Flags(flags)
            .Submit();
    }

    public ZLinkBackendActorJoinRequest? RecvActorJoin(RecvFlags flags)
    {
        var request = nativeSpot.RecvActorJoin(flags);
        if (request is null)
        {
            return null;
        }

        return new ZLinkBackendActorJoinRequest(
            request.Info.SourceActor.ToBackend(),
            request.Info.TargetActor.ToBackend(),
            request.Info.SourceNodeRid,
            request.Info.TargetSpotRid,
            request.Info.JoinEpoch,
            request.Message)
        {
            NativeRequest = request
        };
    }

    public void ReplyActorJoin(
        ZLinkBackendActorJoinRequest request,
        bool accepted,
        Message reply)
    {
        var nativeRequest = (ActorJoinRequest)request.NativeRequest!;
        nativeSpot.ReplyActorJoin(nativeRequest, accepted, reply);
    }

    public ValueTask DisposeAsync() => nativeSpot.DisposeAsync();
}
