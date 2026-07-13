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
        var actor = await actors.GetOrCreateAsync(
            request.PlayerId,
            SampleNames.SessionActorType,
            request,
            cancellationToken);
        _ = await context.Actors.BindOrGetAsync(actor, cancellationToken);
        context.Client.Reply(await joinSessions.ExecuteAsync(request.PlayerId, cancellationToken))
            .Submit();
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
        return await actions.KillMonsterAsync(request, cancellationToken);
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
        return await actions.CollectItemAsync(request, cancellationToken);
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
        return await actions.CompleteMissionAsync(request, cancellationToken);
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
        return await actions.EnterAreaAsync(request, cancellationToken);
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
        return await actions.UnlockFeatureAsync(request, cancellationToken);
    }
}

[ZLinkSpotActorRequestHandler(nameof(SyncQuestProgressReq))]
internal sealed class SyncQuestProgressHandler(GameplayActionService actions)
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
        return await actions.SyncAsync(actor.ActorId, cancellationToken);
    }
}
