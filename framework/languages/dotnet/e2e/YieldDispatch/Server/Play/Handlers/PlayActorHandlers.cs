using Systems.Zlink;
using YieldDispatch.Server.Play.Spots;
using YieldDispatch.Shared;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;

namespace YieldDispatch.Server.Play.Handlers;

[ZLinkSpotActorRequestHandler("ActorYieldReq")]
internal sealed class EntryActorYieldHandler(EvidenceStore evidence)
    : IZLinkEntrySpotActorRequestHandler<YieldEntrySpot, YieldActor, ActorYieldReq, ActorYieldRes>
{
    public async ValueTask<ActorYieldRes> HandleAsync(
        YieldEntrySpot entrySpot,
        YieldActor actor,
        ZLinkSpotActorRequestContext context,
        ActorYieldReq request,
        CancellationToken cancellationToken)
    {
        _ = context;
        var mailboxId = $"actor:{actor.ActorId}";
        evidence.Add(
            $"actor-yield-started|rid={evidence.Rid}|spot={entrySpot.Context.SpotRid}"
            + $"|actor={actor.ActorId}|mailbox={mailboxId}|request={request.RequestId}|handler=actor");
        var call = entrySpot.Context.Outbound.RequestToChannel(
                YieldDispatchNames.DelayChannel,
                new DelayReq(request.RequestId, request.DelayMs, $"actor-{actor.ActorId}"))
            .PacketName("DelayReq")
            .Timeout(TimeSpan.FromSeconds(5));
        evidence.Add(
            $"actor-yield-released|rid={evidence.Rid}|spot={entrySpot.Context.SpotRid}"
            + $"|actor={actor.ActorId}|mailbox={mailboxId}|request={request.RequestId}|handler=actor");
        await call.Async<DelayRes>(cancellationToken);
        evidence.Add(
            $"actor-yield-resumed|rid={evidence.Rid}|spot={entrySpot.Context.SpotRid}"
            + $"|actor={actor.ActorId}|mailbox={mailboxId}|request={request.RequestId}|handler=actor");
        evidence.Add(
            $"actor-yield-completed|rid={evidence.Rid}|spot={entrySpot.Context.SpotRid}"
            + $"|actor={actor.ActorId}|mailbox={mailboxId}|request={request.RequestId}|handler=actor");
        return ActorReplies.Reply("YD-B", request.RequestId, actor, entrySpot, "actor-yield-completed");
    }
}

[ZLinkSpotActorRequestHandler("ActorFastReq")]
internal sealed class EntryActorFastHandler(EvidenceStore evidence)
    : IZLinkEntrySpotActorRequestHandler<YieldEntrySpot, YieldActor, ActorFastReq, ActorYieldRes>
{
    public ValueTask<ActorYieldRes> HandleAsync(
        YieldEntrySpot entrySpot,
        YieldActor actor,
        ZLinkSpotActorRequestContext context,
        ActorFastReq request,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        var mailboxId = $"actor:{actor.ActorId}";
        evidence.Add(
            $"actor-fast-started|rid={evidence.Rid}|spot={entrySpot.Context.SpotRid}"
            + $"|actor={actor.ActorId}|mailbox={mailboxId}|request={request.RequestId}"
            + $"|marker={request.Marker}|handler=actor");
        evidence.Add(
            $"actor-fast-completed|rid={evidence.Rid}|spot={entrySpot.Context.SpotRid}"
            + $"|actor={actor.ActorId}|mailbox={mailboxId}|request={request.RequestId}"
            + $"|marker={request.Marker}|handler=actor");
        return ValueTask.FromResult(ActorReplies.Reply("YD-B", request.RequestId, actor, entrySpot, request.Marker));
    }
}

[ZLinkSpotActorRequestHandler("ActorJoinYieldReq")]
internal sealed class EntryActorJoinYieldHandler(EvidenceStore evidence)
    : IZLinkEntrySpotActorRequestHandler<YieldEntrySpot, YieldActor, ActorJoinYieldReq, ActorYieldRes>
{
    public async ValueTask<ActorYieldRes> HandleAsync(
        YieldEntrySpot entrySpot,
        YieldActor actor,
        ZLinkSpotActorRequestContext context,
        ActorJoinYieldReq request,
        CancellationToken cancellationToken)
    {
        _ = context;
        var mailboxId = $"actor:{actor.ActorId}";
        evidence.Add(
            $"actor-join-yield-started|rid={evidence.Rid}|spot={entrySpot.Context.SpotRid}"
            + $"|actor={actor.ActorId}|mailbox={mailboxId}|request={request.RequestId}|target={request.TargetSpotRid}");
        var call = actor.Context.JoinSpot(
            RoutingId.From(request.TargetSpotRid),
            ZLinkMessage.From(new DelayReq(request.RequestId, 350, "join")));
        evidence.Add(
            $"actor-join-yield-released|rid={evidence.Rid}|spot={entrySpot.Context.SpotRid}"
            + $"|actor={actor.ActorId}|mailbox={mailboxId}|request={request.RequestId}|target={request.TargetSpotRid}");
        var joined = await call.Async(cancellationToken);
        evidence.Add(
            $"actor-join-yield-resumed|rid={evidence.Rid}|spot={entrySpot.Context.SpotRid}"
            + $"|actor={actor.ActorId}|mailbox={mailboxId}|request={request.RequestId}|accepted={joined.Accepted}");
        evidence.Add(
            $"actor-join-yield-completed|rid={evidence.Rid}|spot={entrySpot.Context.SpotRid}"
            + $"|actor={actor.ActorId}|mailbox={mailboxId}|request={request.RequestId}|accepted={joined.Accepted}");
        return ActorReplies.Reply("YD-B3", request.RequestId, actor, entrySpot, "actor-join-yield-completed");
    }
}

[ZLinkSpotActorRequestHandler("ActorPushYieldReq")]
internal sealed class EntryActorPushYieldHandler(EvidenceStore evidence)
    : IZLinkEntrySpotActorRequestHandler<YieldEntrySpot, YieldActor, ActorPushYieldReq, ActorYieldRes>
{
    public async ValueTask<ActorYieldRes> HandleAsync(
        YieldEntrySpot entrySpot,
        YieldActor actor,
        ZLinkSpotActorRequestContext context,
        ActorPushYieldReq request,
        CancellationToken cancellationToken)
    {
        _ = context;
        var mailboxId = $"actor:{actor.ActorId}";
        evidence.Add(
            $"actor-push-yield-started|rid={evidence.Rid}|spot={entrySpot.Context.SpotRid}"
            + $"|actor={actor.ActorId}|mailbox={mailboxId}|request={request.RequestId}|handler=actor");
        var call = entrySpot.Context.Outbound.RequestToChannel(
                YieldDispatchNames.DelayChannel,
                new DelayReq(request.RequestId, request.DelayMs, $"actor-push-{actor.ActorId}"))
            .PacketName("DelayReq")
            .Timeout(TimeSpan.FromSeconds(5));
        evidence.Add(
            $"actor-push-yield-released|rid={evidence.Rid}|spot={entrySpot.Context.SpotRid}"
            + $"|actor={actor.ActorId}|mailbox={mailboxId}|request={request.RequestId}|handler=actor");
        await call.Async<DelayRes>(cancellationToken);
        evidence.Add(
            $"actor-push-yield-resumed|rid={evidence.Rid}|spot={entrySpot.Context.SpotRid}"
            + $"|actor={actor.ActorId}|mailbox={mailboxId}|request={request.RequestId}|handler=actor");
        await actor.Context.BoundSession.Send(
                new ActorPushNotify(
                    actor.ActorId,
                    request.RequestId,
                    request.Value,
                    entrySpot.Context.NodeRid.ToString()))
            .PacketName("ActorPushNotify")
            .Async(cancellationToken);
        evidence.Add(
            $"actor-push-yield-completed|rid={evidence.Rid}|spot={entrySpot.Context.SpotRid}"
            + $"|actor={actor.ActorId}|mailbox={mailboxId}|request={request.RequestId}|handler=actor");
        return ActorReplies.Reply("YD-D4", request.RequestId, actor, entrySpot, "actor-push-yield-completed");
    }
}
