using GameQuest.GameApi.Application;
using GameQuest.GameApi.Infrastructure.Store;
using GameQuest.Server.Configuration;
using GameQuest.Shared;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;

namespace GameQuest.GameApi.Session;

[ZLinkSpotActorRequestHandler(nameof(GetQuestProgressReq))]
internal sealed class GetQuestProgressHandler(GameQuestStore store)
    : IZLinkEntrySpotActorRequestHandler<
        GameQuestEntrySpot,
        PlayerSessionActor,
        GetQuestProgressReq,
        GetQuestProgressRes>
{
    public async ValueTask<GetQuestProgressRes> HandleAsync(
        GameQuestEntrySpot entrySpot,
        PlayerSessionActor actor,
        ZLinkSpotActorRequestContext context,
        GetQuestProgressReq request,
        CancellationToken cancellationToken)
    {
        actor.EnsurePlayer(request.PlayerId);
        return new GetQuestProgressRes(
            await store.ReadProjectionAsync(actor.ActorId, cancellationToken));
    }
}

internal sealed class JoinSessionHandler(
    JoinQuestSessionUseCase joinSessions,
    IZLinkActorManager actors)
    : IZLinkSessionPacketHandler<IZLinkSessionContext, JoinSessionReq>
{
    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        JoinSessionReq request,
        CancellationToken cancellationToken)
    {
        var actor = (await actors.GetOrCreate(request.PlayerId, SampleNames.SessionActorType)
            .Request(request).Async(cancellationToken)) switch
        {
            ZLinkActorCreateResult.Existing value => value.Actor,
            ZLinkActorCreateResult.Created value => value.Actor,
            _ => throw new InvalidOperationException("Session Actor creation was rejected.")
        };
        _ = await context.Actors.BindOrGetAsync(actor, cancellationToken);
        await context.Client.Reply(await joinSessions.ExecuteAsync(request.PlayerId, cancellationToken))
            .Async(cancellationToken);
    }
}

[ZLinkSpotActorRequestHandler(nameof(KillMonsterReq))]
internal sealed class KillMonsterHandler(GameplayActionService actions)
    : IZLinkEntrySpotActorRequestHandler<GameQuestEntrySpot, PlayerSessionActor, KillMonsterReq, KillMonsterRes>
{
    public async ValueTask<KillMonsterRes> HandleAsync(
        GameQuestEntrySpot entrySpot,
        PlayerSessionActor actor,
        ZLinkSpotActorRequestContext context,
        KillMonsterReq request,
        CancellationToken cancellationToken)
    {
        actor.EnsurePlayer(request.PlayerId);
        return new KillMonsterRes(await actions.KillMonsterAsync(
            request.PlayerId,
            request.MonsterId,
            request.AreaId,
            request.IdempotencyKey,
            cancellationToken));
    }
}

[ZLinkSpotActorRequestHandler(nameof(CollectItemReq))]
internal sealed class CollectItemHandler(GameplayActionService actions)
    : IZLinkEntrySpotActorRequestHandler<GameQuestEntrySpot, PlayerSessionActor, CollectItemReq, CollectItemRes>
{
    public async ValueTask<CollectItemRes> HandleAsync(
        GameQuestEntrySpot entrySpot,
        PlayerSessionActor actor,
        ZLinkSpotActorRequestContext context,
        CollectItemReq request,
        CancellationToken cancellationToken)
    {
        actor.EnsurePlayer(request.PlayerId);
        return new CollectItemRes(await actions.CollectItemAsync(
            request.PlayerId,
            request.ItemId,
            request.Count,
            request.IdempotencyKey,
            cancellationToken));
    }
}

[ZLinkSpotActorRequestHandler(nameof(CompleteMissionReq))]
internal sealed class CompleteMissionHandler(GameplayActionService actions)
    : IZLinkEntrySpotActorRequestHandler<GameQuestEntrySpot, PlayerSessionActor, CompleteMissionReq, CompleteMissionRes>
{
    public async ValueTask<CompleteMissionRes> HandleAsync(
        GameQuestEntrySpot entrySpot,
        PlayerSessionActor actor,
        ZLinkSpotActorRequestContext context,
        CompleteMissionReq request,
        CancellationToken cancellationToken)
    {
        actor.EnsurePlayer(request.PlayerId);
        return new CompleteMissionRes(await actions.CompleteMissionAsync(
            request.PlayerId,
            request.MissionId,
            request.IdempotencyKey,
            cancellationToken));
    }
}

[ZLinkSpotActorRequestHandler(nameof(EnterAreaReq))]
internal sealed class EnterAreaHandler(GameplayActionService actions)
    : IZLinkEntrySpotActorRequestHandler<GameQuestEntrySpot, PlayerSessionActor, EnterAreaReq, EnterAreaRes>
{
    public async ValueTask<EnterAreaRes> HandleAsync(
        GameQuestEntrySpot entrySpot,
        PlayerSessionActor actor,
        ZLinkSpotActorRequestContext context,
        EnterAreaReq request,
        CancellationToken cancellationToken)
    {
        actor.EnsurePlayer(request.PlayerId);
        return new EnterAreaRes(await actions.EnterAreaAsync(
            request.PlayerId,
            request.AreaId,
            request.IdempotencyKey,
            cancellationToken));
    }
}

[ZLinkSpotActorRequestHandler(nameof(UnlockFeatureReq))]
internal sealed class UnlockFeatureHandler(GameplayActionService actions)
    : IZLinkEntrySpotActorRequestHandler<GameQuestEntrySpot, PlayerSessionActor, UnlockFeatureReq, UnlockFeatureRes>
{
    public async ValueTask<UnlockFeatureRes> HandleAsync(
        GameQuestEntrySpot entrySpot,
        PlayerSessionActor actor,
        ZLinkSpotActorRequestContext context,
        UnlockFeatureReq request,
        CancellationToken cancellationToken)
    {
        actor.EnsurePlayer(request.PlayerId);
        return new UnlockFeatureRes(await actions.UnlockFeatureAsync(
            request.PlayerId,
            request.FeatureId,
            request.IdempotencyKey,
            cancellationToken));
    }
}

[ZLinkSpotActorRequestHandler(nameof(SyncQuestProgressReq))]
internal sealed class SyncQuestProgressHandler(IQuestProgressSynchronizer quests)
    : IZLinkEntrySpotActorRequestHandler<
        GameQuestEntrySpot,
        PlayerSessionActor,
        SyncQuestProgressReq,
        SyncQuestProgressRes>
{
    public async ValueTask<SyncQuestProgressRes> HandleAsync(
        GameQuestEntrySpot entrySpot,
        PlayerSessionActor actor,
        ZLinkSpotActorRequestContext context,
        SyncQuestProgressReq request,
        CancellationToken cancellationToken)
    {
        actor.EnsurePlayer(request.PlayerId);
        return await quests.SyncAsync(actor.ActorId, cancellationToken);
    }
}
