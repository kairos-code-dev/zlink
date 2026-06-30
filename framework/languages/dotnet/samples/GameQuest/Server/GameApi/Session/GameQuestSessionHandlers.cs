using GameQuest.GameApi.Application;
using GameQuest.GameApi.Infrastructure.Store;
using GameQuest.Shared;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Streams;

namespace GameQuest.GameApi.Session;

internal sealed class SubscribeQuestHandler(GameQuestStore store, GameQuestSessionRegistry registry)
    : IZLinkSessionPacketHandler<IZLinkSessionContext>
{
    public string PacketName => nameof(SubscribeQuestReq);

    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        var request = payload.Decode<SubscribeQuestReq>();
        await store.BindSessionAsync(registry.Bind(request.PlayerId, context), cancellationToken);
        var projection = await store.ReadProjectionAsync(request.PlayerId, cancellationToken);
        context.Client.Reply(new SubscribeQuestRes(projection)).Submit();
    }
}

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
