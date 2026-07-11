namespace Zlink.Framework.Runtime.Backend.DotNet.Wrappers;

internal sealed class ZLinkBackendSpotWrapper(ISpot nativeSpot) : IZLinkBackendSpot
{
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
                frameworkInfo = frameworkInfo with
                {
                    RoutedMessages = DrainRoutedMessages()
                };

            handler(frameworkInfo);
        });
    }

    public void OnSendReady(Action handler)
    {
        nativeSpot.SetSendReadyHandler(() => handler());
    }

    public bool RequestToChannel(
        string channelName,
        Message message,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout)
    {
        var operation = nativeSpot.RequestToChannel(channelName)
            .Message(message)
            .Flags(flags);
        if (timeout is { } value) operation = operation.Timeout(value);

        return operation.Submit(callback);
    }

    public bool RequestToChannel(
        string channelName,
        IReadOnlyList<Message> parts,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout)
    {
        var operation = nativeSpot.RequestToChannel(channelName).Messages(parts);

        if (timeout is { } value) operation = operation.Timeout(value);

        return operation.Flags(flags).Submit(callback);
    }

    public bool SendToChannel(
        string channelName,
        Message message,
        SendFlags flags)
    {
        return nativeSpot.SendToChannel(channelName)
            .Message(message)
            .Flags(flags)
            .Submit();
    }

    public bool SendToChannel(
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
        RoutingId targetSpotRid,
        Message message,
        SendFlags flags)
    {
        return nativeSpot.SendToSpot(targetRid, targetSpotRid)
            .Message(message)
            .Flags(flags)
            .Submit();
    }

    public bool SendToSpot(
        RoutingId targetRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        return nativeSpot.SendToSpot(targetRid, targetSpotRid)
            .Messages(parts)
            .Flags(flags)
            .Submit();
    }

    public bool RequestToSpot(
        RoutingId targetRid,
        RoutingId targetSpotRid,
        Message message,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout)
    {
        var operation = nativeSpot.RequestToSpot(targetRid, targetSpotRid)
            .Message(message)
            .Flags(flags);
        if (timeout is { } value) operation = operation.Timeout(value);

        return operation.Submit(callback);
    }

    public bool RequestToSpot(
        RoutingId targetRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout)
    {
        var operation = nativeSpot.RequestToSpot(targetRid, targetSpotRid).Messages(parts);

        if (timeout is { } value) operation = operation.Timeout(value);

        return operation.Flags(flags).Submit(callback);
    }

    public ZLinkBackendActorJoinRequest? RecvActorJoin(RecvFlags flags)
    {
        var request = nativeSpot.RecvActorJoin(flags);
        if (request is null) return null;

        return new ZLinkDotNetBackendActorJoinRequest(
            request.Info.SourceActor.ToBackend(),
            request.Info.TargetActor.ToBackend(),
            request.Info.SourceNodeRid,
            request.Info.TargetSpotRid,
            request.Info.JoinEpoch,
            request.Message,
            request.Parts,
            request);
    }

    public ZLinkBackendSpotActorLifecycleEvent? RecvActorLifecycle(RecvFlags flags)
    {
        var lifecycle = nativeSpot.RecvActorLifecycle(flags);
        return lifecycle?.ToFramework();
    }

    public void ReplyActorJoin(
        ZLinkBackendActorJoinRequest request,
        int joinResultCode,
        Message reply)
    {
        var nativeRequest = RequireNativeRequest(request);
        nativeSpot.ReplyActorJoin(nativeRequest, joinResultCode)
            .Message(reply)
            .Submit();
    }

    public void ReplyActorJoin(
        ZLinkBackendActorJoinRequest request,
        int joinResultCode,
        IReadOnlyList<Message> parts)
    {
        var nativeRequest = RequireNativeRequest(request);
        nativeSpot.ReplyActorJoin(nativeRequest, joinResultCode)
            .Messages(parts)
            .Submit();
    }

    private static ActorJoinRequest RequireNativeRequest(ZLinkBackendActorJoinRequest request) =>
        request is ZLinkDotNetBackendActorJoinRequest dotNetRequest
            ? dotNetRequest.NativeRequest
            : throw new InvalidOperationException("Expected a .NET backend actor join request.");

    private sealed class ZLinkDotNetBackendActorJoinRequest(
        ZLinkBackendActorRef sourceActor,
        ZLinkBackendActorRef targetActor,
        RoutingId sourceNodeRid,
        RoutingId targetSpotRid,
        ulong joinEpoch,
        Message message,
        IReadOnlyList<Message> parts,
        ActorJoinRequest nativeRequest)
        : ZLinkBackendActorJoinRequest(
            sourceActor,
            targetActor,
            sourceNodeRid,
            targetSpotRid,
            joinEpoch,
            message,
            parts)
    {
        public ActorJoinRequest NativeRequest { get; } = nativeRequest;
    }

    public ValueTask DisposeAsync()
    {
        return nativeSpot.DisposeAsync();
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
}
