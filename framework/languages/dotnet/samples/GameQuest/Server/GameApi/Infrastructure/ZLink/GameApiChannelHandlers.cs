using GameQuest.GameApi.Infrastructure.Store;
using GameQuest.GameApi.Session;
using GameQuest.Server.Configuration;
using GameQuest.Shared;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Actors;

namespace GameQuest.GameApi.Infrastructure.ZLink;

[ZLinkHandlerGroup(SampleNames.GameApiHandlerGroup)]
internal sealed class QuestProgressNotifyHandler(
    IZLinkActorManager actors,
    IZLinkActorClient actorClient)
    : IZLinkSendHandler<QuestProgressNotify>
{
    public async ValueTask HandleAsync(
        QuestProgressNotify request,
        ZLinkSendContext context,
        CancellationToken cancellationToken)
    {
        var actor = await actors.FindAsync(request.PlayerId, cancellationToken);
        if (actor is null) return;

        await actorClient.SendToActor(
            SampleNames.MeshName, actor.Value, request).Async(cancellationToken);
    }
}

[ZLinkHandlerGroup(SampleNames.GameApiHandlerGroup)]
internal sealed class QuestCompletedNotifyHandler(
    IZLinkActorManager actors,
    IZLinkActorClient actorClient)
    : IZLinkSendHandler<QuestCompletedNotify>
{
    public async ValueTask HandleAsync(
        QuestCompletedNotify request,
        ZLinkSendContext context,
        CancellationToken cancellationToken)
    {
        var actor = await actors.FindAsync(request.PlayerId, cancellationToken);
        if (actor is null) return;

        await actorClient.SendToActor(
            SampleNames.MeshName, actor.Value, request).Async(cancellationToken);
    }
}
