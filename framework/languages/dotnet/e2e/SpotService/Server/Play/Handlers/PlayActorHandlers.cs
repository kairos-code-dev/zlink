using SpotService.Server.Play.Spots;
using SpotService.Shared;
using Systems.Zlink;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;

namespace SpotService.Server.Play.Handlers;

[ZLinkHandlerGroup("client")]
internal sealed class ChannelEchoHandler(EvidenceStore evidence)
    : IZLinkRequestHandler<ChannelEchoReq, ChannelEchoRes>
{
    public ValueTask<ChannelEchoRes> HandleAsync(
        ChannelEchoReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"channel-echo|value={request.Value}");
        return ValueTask.FromResult(new ChannelEchoRes($"echo-{request.Value}"));
    }
}

[ZLinkHandlerGroup("client")]
internal sealed class ChannelNotifyHandler(EvidenceStore evidence)
    : IZLinkSendHandler<ChannelNotify>
{
    public ValueTask HandleAsync(
        ChannelNotify message,
        ZLinkSendContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"channel-notify|marker={message.Marker}");
        return ValueTask.CompletedTask;
    }
}

[ZLinkSpotActorRequestHandler("ActorPingReq")]
internal sealed class EntryActorPingHandler(EvidenceStore evidence)
    : IZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, ActorPingReq, ActorPingRes>
{
    public ValueTask<ActorPingRes> HandleAsync(
        ScenarioEntrySpot entrySpot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        ActorPingReq request,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        actor.Seen++;
        evidence.Add(
            $"actor-ping|rid={entrySpot.Context.NodeRid}|actor={actor.ActorId}"
            + $"|spot={entrySpot.Context.SpotRid}|value={request.Value}|seen={actor.Seen}");
        return ValueTask.FromResult(new ActorPingRes(
            actor.ActorId,
            entrySpot.Context.NodeRid.ToString(),
            entrySpot.Context.SpotRid.ToString(),
            request.Value,
            actor.Seen));
    }
}

[ZLinkSpotActorRequestHandler("SlowActorPingReq")]
internal sealed class EntrySlowActorPingHandler(EvidenceStore evidence)
    : IZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, SlowActorPingReq, ActorPingRes>
{
    public async ValueTask<ActorPingRes> HandleAsync(
        ScenarioEntrySpot entrySpot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        SlowActorPingReq request,
        CancellationToken cancellationToken)
    {
        _ = context;
        await Task.Delay(TimeSpan.FromMilliseconds(request.DelayMs), cancellationToken);
        actor.Seen++;
        evidence.Add(
            $"actor-slow-ping|rid={entrySpot.Context.NodeRid}|actor={actor.ActorId}"
            + $"|spot={entrySpot.Context.SpotRid}|value={request.Value}|seen={actor.Seen}");
        return new ActorPingRes(
            actor.ActorId,
            entrySpot.Context.NodeRid.ToString(),
            entrySpot.Context.SpotRid.ToString(),
            request.Value,
            actor.Seen);
    }
}

[ZLinkSpotActorRequestHandler("UserActorPingReq")]
internal sealed class EntryUserActorPingHandler(EvidenceStore evidence)
    : IZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, ActorPingReq, ActorPingRes>
{
    public ValueTask<ActorPingRes> HandleAsync(
        ScenarioEntrySpot entrySpot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        ActorPingReq request,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        actor.Seen++;
        evidence.Add(
            $"actor-ping|rid={entrySpot.Context.NodeRid}|actor={actor.ActorId}"
            + $"|spot={actor.DisplayName}|value={request.Value}|seen={actor.Seen}");
        return ValueTask.FromResult(new ActorPingRes(
            actor.ActorId,
            entrySpot.Context.NodeRid.ToString(),
            actor.DisplayName,
            request.Value,
            actor.Seen));
    }
}

[ZLinkSpotActorRequestHandler("UserActorPingReq")]
internal sealed class UserActorPingHandler(EvidenceStore evidence)
    : IZLinkSpotActorRequestHandler<ScenarioUserSpot, ScenarioActor, ActorPingReq, ActorPingRes>
{
    public ValueTask<ActorPingRes> HandleAsync(
        ScenarioUserSpot spot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        ActorPingReq request,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        actor.Seen++;
        evidence.Add(
            $"actor-ping|rid={spot.Context.NodeRid}|actor={actor.ActorId}"
            + $"|spot={spot.Context.SpotRid}|value={request.Value}|seen={actor.Seen}");
        return ValueTask.FromResult(new ActorPingRes(
            actor.ActorId,
            spot.Context.NodeRid.ToString(),
            spot.Context.SpotRid.ToString(),
            request.Value,
            actor.Seen));
    }
}

[ZLinkSpotActorRequestHandler("JoinUserSpotActorReq")]
internal sealed class EntryUserSpotActorJoinHandler
    : IZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, JoinUserSpotActorReq, JoinUserSpotActorRes>
{
    public async ValueTask<JoinUserSpotActorRes> HandleAsync(
        ScenarioEntrySpot entrySpot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        JoinUserSpotActorReq request,
        CancellationToken cancellationToken)
    {
        _ = context;
        if (!string.Equals(request.ActorId, actor.ActorId, StringComparison.Ordinal))
            throw new InvalidOperationException("Join request actor does not match dispatched actor.");

        var joined = await actor.Context.JoinSpot(
                RoutingId.From(request.SpotRid),
                ZLinkMessage.Empty)
            .Async(cancellationToken)
            .ConfigureAwait(false);
        return new JoinUserSpotActorRes(
            request.SpotRid,
            joined.Actor.ActorId,
            true,
            joined.Actor.Generation);
    }
}

[ZLinkSpotActorRequestHandler("LeaveReq")]
internal sealed class EntryActorLeaveHandler(EvidenceStore evidence)
    : IZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, LeaveReq, LeaveRes>
{
    public ValueTask<LeaveRes> HandleAsync(
        ScenarioEntrySpot entrySpot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        LeaveReq request,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        if (!string.Equals(request.ActorId, actor.ActorId, StringComparison.Ordinal))
            throw new InvalidOperationException("Leave request actor does not match dispatched actor.");

        evidence.Add(
            $"spot-actor-left|rid={entrySpot.Context.NodeRid}|spot={actor.DisplayName}|actor={actor.ActorId}");
        return ValueTask.FromResult(new LeaveRes(actor.ActorId, true));
    }
}

[ZLinkSpotActorRequestHandler("LeaveReq")]
internal sealed class UserActorLeaveHandler
    : IZLinkSpotActorRequestHandler<ScenarioUserSpot, ScenarioActor, LeaveReq, LeaveRes>
{
    public async ValueTask<LeaveRes> HandleAsync(
        ScenarioUserSpot spot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        LeaveReq request,
        CancellationToken cancellationToken)
    {
        _ = context;
        if (!string.Equals(request.ActorId, actor.ActorId, StringComparison.Ordinal))
            throw new InvalidOperationException("Leave request actor does not match dispatched actor.");

        await spot.Context.leaveActor(actor, cancellationToken);
        return new LeaveRes(actor.ActorId, true);
    }
}

[ZLinkSpotActorRequestHandler("SnapshotReq")]
internal sealed class EntryActorSnapshotHandler
    : IZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, SnapshotReq, SnapshotRes>
{
    public ValueTask<SnapshotRes> HandleAsync(
        ScenarioEntrySpot entrySpot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        SnapshotReq request,
        CancellationToken cancellationToken)
    {
        _ = entrySpot;
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        if (!string.Equals(request.ActorId, actor.ActorId, StringComparison.Ordinal))
            throw new InvalidOperationException("Snapshot request actor does not match dispatched actor.");

        return ValueTask.FromResult(new SnapshotRes(actor.ActorId, actor.Seen));
    }
}

[ZLinkSpotActorRequestHandler("DestroyActorReq")]
internal sealed class EntryActorDestroyHandler(EvidenceStore evidence)
    : IZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, DestroyActorReq, DestroyActorRes>
{
    public ValueTask<DestroyActorRes> HandleAsync(
        ScenarioEntrySpot entrySpot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        DestroyActorReq request,
        CancellationToken cancellationToken)
    {
        _ = context;
        if (!string.Equals(request.ActorId, actor.ActorId, StringComparison.Ordinal))
            throw new InvalidOperationException("Destroy request actor does not match dispatched actor.");

        entrySpot.Context.RunWorker(_ => true).Submit(
            async (_, ct) =>
            {
                try
                {
                    await entrySpot.Context.DestroyActorAsync(actor, ct);
                    evidence.Add($"actor-destroyed|rid={evidence.Rid}|actor={actor.ActorId}");
                }
                catch (Exception ex)
                {
                    evidence.Add(
                        $"actor-destroy-failed|rid={evidence.Rid}|actor={actor.ActorId}|error={ex.GetType().Name}");
                }
            },
            cancellationToken: cancellationToken);
        return ValueTask.FromResult(new DestroyActorRes(actor.ActorId, true));
    }
}

[ZLinkSpotActorRequestHandler("ActorPushReq")]
internal sealed class ActorPushHandler
    : IZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, ActorPushReq, ActorPingRes>
{
    public async ValueTask<ActorPingRes> HandleAsync(
        ScenarioEntrySpot entrySpot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        ActorPushReq request,
        CancellationToken cancellationToken)
    {
        actor.Seen++;
        actor.Context.BoundSession.Send(new ActorPushNotify(actor.ActorId, request.Value, actor.Seen))
            .PacketName("ActorPushNotify").Submit();
        return new ActorPingRes(
            actor.ActorId,
            entrySpot.Context.NodeRid.ToString(),
            entrySpot.Context.SpotRid.ToString(),
            request.Value,
            actor.Seen);
    }
}

[ZLinkSpotActorRequestHandler("UserActorPushReq")]
internal sealed class EntryUserActorPushHandler
    : IZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, ActorPushReq, ActorPingRes>
{
    public async ValueTask<ActorPingRes> HandleAsync(
        ScenarioEntrySpot entrySpot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        ActorPushReq request,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        actor.Seen++;
        actor.Context.BoundSession.Send(new ActorPushNotify(actor.ActorId, request.Value, actor.Seen))
            .PacketName("ActorPushNotify").Submit();
        return new ActorPingRes(
            actor.ActorId,
            entrySpot.Context.NodeRid.ToString(),
            actor.DisplayName,
            request.Value,
            actor.Seen);
    }
}

[ZLinkSpotActorRequestHandler("UserActorPushReq")]
internal sealed class UserActorPushHandler
    : IZLinkSpotActorRequestHandler<ScenarioUserSpot, ScenarioActor, ActorPushReq, ActorPingRes>
{
    public async ValueTask<ActorPingRes> HandleAsync(
        ScenarioUserSpot spot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        ActorPushReq request,
        CancellationToken cancellationToken)
    {
        _ = context;
        actor.Seen++;
        actor.Context.BoundSession.Send(new ActorPushNotify(actor.ActorId, request.Value, actor.Seen))
            .PacketName("ActorPushNotify").Submit();
        return new ActorPingRes(
            actor.ActorId,
            spot.Context.NodeRid.ToString(),
            spot.Context.SpotRid.ToString(),
            request.Value,
            actor.Seen);
    }
}

[ZLinkSpotActorRequestHandler("ComplexActorReq")]
internal sealed class ComplexActorHandler(EvidenceStore evidence)
    : IZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, ComplexActorReq, ComplexActorRes>
{
    public ValueTask<ComplexActorRes> HandleAsync(
        ScenarioEntrySpot entrySpot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        ComplexActorReq request,
        CancellationToken cancellationToken)
    {
        _ = entrySpot;
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        actor.DisplayName = request.DisplayName;
        evidence.Add(
            $"actor-complex|rid={evidence.Rid}|actor={actor.ActorId}|name={request.DisplayName}"
            + $"|level={request.Level}|tags={string.Join(",", request.Tags)}"
            + $"|attrs={string.Join(",", request.Attributes.OrderBy(static pair => pair.Key).Select(static pair => $"{pair.Key}:{pair.Value}"))}");
        return ValueTask.FromResult(new ComplexActorRes(
            actor.ActorId,
            request.DisplayName,
            request.Level,
            request.Tags,
            request.Attributes));
    }
}
