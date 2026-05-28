namespace Zlink.Framework.Runtime.Backend.DotNet.Wrappers;


internal sealed class ZLinkBackendSpotWrapper(ISpot nativeSpot) : IZLinkBackendSpot
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

    public bool Subscribe(TopicMessage result, RecvFlags flags)
    {
        return nativeSpot.Subscribe(result, flags);
    }

    public bool RecvRoute(Received result, RecvFlags flags)
    {
        return nativeSpot.RecvRouted(result, flags);
    }

    public void OnDispatchEvent(Action<ZLinkBackendSpotDispatchInfo> handler)
    {
        nativeSpot.SetDispatchHandler(info =>
        {
            var frameworkInfo = info.ToFramework();
            if (info.Event == SpotDispatchEvent.RoutedReadable)
            {
                frameworkInfo = frameworkInfo with
                {
                    RoutedMessages = DrainRoutedMessages()
                };
            }

            handler(frameworkInfo);
        });
    }

    private IReadOnlyList<Received> DrainRoutedMessages()
    {
        List<Received>? receivedMessages = null;
        while (true)
        {
            var received = Received.Create();
            if (!nativeSpot.RecvRouted(received, RecvFlags.DontWait))
            {
                received.Dispose();
                break;
            }

            receivedMessages ??= [];
            receivedMessages.Add(received);
        }

        return receivedMessages ?? [];
    }

    public void OnSendReady(Action handler)
    {
        nativeSpot.SetSendReadyHandler(() => handler());
    }

    public bool RequestChannel(
        string channelName,
        Message message,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout)
    {
        var operation = nativeSpot.RequestToChannel(channelName)
            .Message(message)
            .Flags(flags);
        if (timeout is { } value)
        {
            operation = operation.Timeout(value);
        }

        return operation.Submit(callback);
    }

    public bool RequestChannel(
        string channelName,
        IReadOnlyList<Message> parts,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout)
    {
        var operation = nativeSpot.RequestToChannel(channelName).Messages(parts);

        if (timeout is { } value)
        {
            operation = operation.Timeout(value);
        }

        return operation.Flags(flags).Submit(callback);
    }

    public bool SendChannel(
        string channelName,
        Message message,
        SendFlags flags)
    {
        return nativeSpot.SendToChannel(channelName)
            .Message(message)
            .Flags(flags)
            .Submit();
    }

    public bool SendChannel(
        string channelName,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        return nativeSpot.SendToChannel(channelName)
            .Messages(parts)
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

    public bool Publish(
        string topic,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        return nativeSpot.Publish(topic)
            .Messages(parts)
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

    public bool SendToSpot(
        RoutingId targetRid,
        RoutingId spotRid,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        return nativeSpot.SendToSpot(targetRid, spotRid)
            .Messages(parts)
            .Flags(flags)
            .Submit();
    }

    public bool RequestToSpot(
        RoutingId targetRid,
        RoutingId spotRid,
        Message message,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout)
    {
        var operation = nativeSpot.RequestToSpot(targetRid, spotRid)
            .Message(message)
            .Flags(flags);
        if (timeout is { } value)
        {
            operation = operation.Timeout(value);
        }

        return operation.Submit(callback);
    }

    public bool RequestToSpot(
        RoutingId targetRid,
        RoutingId spotRid,
        IReadOnlyList<Message> parts,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout)
    {
        var operation = nativeSpot.RequestToSpot(targetRid, spotRid).Messages(parts);

        if (timeout is { } value)
        {
            operation = operation.Timeout(value);
        }

        return operation.Flags(flags).Submit(callback);
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
            request.Message,
            request.Parts)
        {
            NativeRequest = request
        };
    }

    public void ReplyActorJoin(
        ZLinkBackendActorJoinRequest request,
        int joinResultCode,
        Message reply)
    {
        var nativeRequest = (ActorJoinRequest)request.NativeRequest!;
        nativeSpot.ReplyActorJoin(nativeRequest, joinResultCode)
            .Message(reply)
            .Submit();
    }

    public void ReplyActorJoin(
        ZLinkBackendActorJoinRequest request,
        int joinResultCode,
        IReadOnlyList<Message> parts)
    {
        var nativeRequest = (ActorJoinRequest)request.NativeRequest!;
        nativeSpot.ReplyActorJoin(nativeRequest, joinResultCode)
            .Messages(parts)
            .Submit();
    }

    public ValueTask DisposeAsync() => nativeSpot.DisposeAsync();
}
