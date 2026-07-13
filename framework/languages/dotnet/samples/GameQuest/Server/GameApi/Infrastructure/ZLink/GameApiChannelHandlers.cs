using GameQuest.GameApi.Infrastructure.Store;
using GameQuest.GameApi.Session;
using GameQuest.Server.Configuration;
using GameQuest.Shared;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Actors;

namespace GameQuest.GameApi.Infrastructure.ZLink;

[ZLinkHandlerGroup("game-api")]
internal sealed class GetGameplaySnapshotHandler(GameQuestStore store)
    : IZLinkRequestHandler<GetGameplaySnapshotReq, GetGameplaySnapshotRes>
{
    public ValueTask<GetGameplaySnapshotRes> HandleAsync(
        GetGameplaySnapshotReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        return store.ReadSnapshotAsync(request.PlayerId, cancellationToken);
    }
}

[ZLinkHandlerGroup("game-api")]
internal sealed class NotifyQuestProgressHandler(
    IZLinkActorDirectory actors,
    IZLinkActorClient actorClient)
    : IZLinkRequestHandler<NotifyQuestProgressReq, NotifyQuestProgressRes>
{
    public async ValueTask<NotifyQuestProgressRes> HandleAsync(
        NotifyQuestProgressReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        var actor = await actors.FindAsync(request.PlayerId, cancellationToken);
        if (actor is null) return new NotifyQuestProgressRes(false);

        actorClient.SendToActor(actor.Value, new NotifyQuestProgressActorReq(request))
            .Submit(cancellationToken);
        return new NotifyQuestProgressRes(true);
    }
}
