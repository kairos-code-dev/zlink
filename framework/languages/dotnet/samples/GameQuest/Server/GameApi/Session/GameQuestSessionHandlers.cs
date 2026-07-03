using GameQuest.GameApi.Application;
using GameQuest.GameApi.Infrastructure.Store;
using GameQuest.Shared;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Streams;

namespace GameQuest.GameApi.Session;

internal sealed class GetQuestProgressHandler(GameQuestStore store)
    : IZLinkSessionPacketHandler<IZLinkSessionContext>
{
    public string PacketName => nameof(GetQuestProgressReq);

    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        var request = payload.Decode<GetQuestProgressReq>();
        context.Client.Reply(new GetQuestProgressRes(
                await store.ReadProjectionAsync(request.PlayerId, cancellationToken)))
            .Submit();
    }
}

internal sealed class JoinSessionHandler(JoinQuestSessionUseCase joinSessions, GameQuestSessionRegistry registry)
    : IZLinkSessionPacketHandler<IZLinkSessionContext>
{
    public string PacketName => nameof(JoinSessionReq);

    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        var request = payload.Decode<JoinSessionReq>();
        context.Client.Reply(await joinSessions.ExecuteAsync(registry.Bind(request.PlayerId, context), cancellationToken))
            .Submit();
    }
}

internal sealed class KillMonsterHandler(GameplayActionService actions)
    : IZLinkSessionPacketHandler<IZLinkSessionContext>
{
    public string PacketName => nameof(KillMonsterReq);

    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        context.Client.Reply(await actions.KillMonsterAsync(payload.Decode<KillMonsterReq>(), cancellationToken))
            .Submit();
    }
}

internal sealed class CollectItemHandler(GameplayActionService actions)
    : IZLinkSessionPacketHandler<IZLinkSessionContext>
{
    public string PacketName => nameof(CollectItemReq);

    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        context.Client.Reply(await actions.CollectItemAsync(payload.Decode<CollectItemReq>(), cancellationToken))
            .Submit();
    }
}

internal sealed class CompleteMissionHandler(GameplayActionService actions)
    : IZLinkSessionPacketHandler<IZLinkSessionContext>
{
    public string PacketName => nameof(CompleteMissionReq);

    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        context.Client.Reply(await actions.CompleteMissionAsync(payload.Decode<CompleteMissionReq>(), cancellationToken))
            .Submit();
    }
}

internal sealed class EnterAreaHandler(GameplayActionService actions)
    : IZLinkSessionPacketHandler<IZLinkSessionContext>
{
    public string PacketName => nameof(EnterAreaReq);

    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        context.Client.Reply(await actions.EnterAreaAsync(payload.Decode<EnterAreaReq>(), cancellationToken))
            .Submit();
    }
}

internal sealed class UnlockFeatureHandler(GameplayActionService actions)
    : IZLinkSessionPacketHandler<IZLinkSessionContext>
{
    public string PacketName => nameof(UnlockFeatureReq);

    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        context.Client.Reply(await actions.UnlockFeatureAsync(payload.Decode<UnlockFeatureReq>(), cancellationToken))
            .Submit();
    }
}

internal sealed class SyncQuestProgressHandler(GameplayActionService actions)
    : IZLinkSessionPacketHandler<IZLinkSessionContext>
{
    public string PacketName => nameof(SyncQuestProgressReq);

    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        var request = payload.Decode<SyncQuestProgressReq>();
        context.Client.Reply(await actions.SyncAsync(request.PlayerId, cancellationToken))
            .Submit();
    }
}
