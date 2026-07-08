using GameQuest.GameApi.Application;
using GameQuest.GameApi.Infrastructure.Store;
using GameQuest.Shared;
using Zlink.Framework.Contracts.Streams;

namespace GameQuest.GameApi.Session;

internal sealed class GetQuestProgressHandler(GameQuestStore store)
    : IZLinkSessionPacketHandler<IZLinkSessionContext, GetQuestProgressReq>
{
    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        GetQuestProgressReq request,
        CancellationToken cancellationToken)
    {
        context.Client.Reply(new GetQuestProgressRes(
                await store.ReadProjectionAsync(request.PlayerId, cancellationToken)))
            .Submit();
    }
}

internal sealed class JoinSessionHandler(JoinQuestSessionUseCase joinSessions, GameQuestSessionRegistry registry)
    : IZLinkSessionPacketHandler<IZLinkSessionContext, JoinSessionReq>
{
    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        JoinSessionReq request,
        CancellationToken cancellationToken)
    {
        context.Client.Reply(await joinSessions.ExecuteAsync(registry.Bind(request.PlayerId, context), cancellationToken))
            .Submit();
    }
}

internal sealed class KillMonsterHandler(GameplayActionService actions)
    : IZLinkSessionPacketHandler<IZLinkSessionContext, KillMonsterReq>
{
    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        KillMonsterReq request,
        CancellationToken cancellationToken)
    {
        context.Client.Reply(await actions.KillMonsterAsync(request, cancellationToken))
            .Submit();
    }
}

internal sealed class CollectItemHandler(GameplayActionService actions)
    : IZLinkSessionPacketHandler<IZLinkSessionContext, CollectItemReq>
{
    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        CollectItemReq request,
        CancellationToken cancellationToken)
    {
        context.Client.Reply(await actions.CollectItemAsync(request, cancellationToken))
            .Submit();
    }
}

internal sealed class CompleteMissionHandler(GameplayActionService actions)
    : IZLinkSessionPacketHandler<IZLinkSessionContext, CompleteMissionReq>
{
    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        CompleteMissionReq request,
        CancellationToken cancellationToken)
    {
        context.Client.Reply(await actions.CompleteMissionAsync(request, cancellationToken))
            .Submit();
    }
}

internal sealed class EnterAreaHandler(GameplayActionService actions)
    : IZLinkSessionPacketHandler<IZLinkSessionContext, EnterAreaReq>
{
    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        EnterAreaReq request,
        CancellationToken cancellationToken)
    {
        context.Client.Reply(await actions.EnterAreaAsync(request, cancellationToken))
            .Submit();
    }
}

internal sealed class UnlockFeatureHandler(GameplayActionService actions)
    : IZLinkSessionPacketHandler<IZLinkSessionContext, UnlockFeatureReq>
{
    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        UnlockFeatureReq request,
        CancellationToken cancellationToken)
    {
        context.Client.Reply(await actions.UnlockFeatureAsync(request, cancellationToken))
            .Submit();
    }
}

internal sealed class SyncQuestProgressHandler(GameplayActionService actions)
    : IZLinkSessionPacketHandler<IZLinkSessionContext, SyncQuestProgressReq>
{
    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        SyncQuestProgressReq request,
        CancellationToken cancellationToken)
    {
        context.Client.Reply(await actions.SyncAsync(request.PlayerId, cancellationToken))
            .Submit();
    }
}
