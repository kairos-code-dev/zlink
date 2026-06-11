using GameQuest.GameApi.Adapters.Store;
using GameQuest.GameApi.Application;
using GameQuest.Shared;
using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.Contracts.Streams;

namespace GameQuest.GameApi.Session;

internal sealed class SubscribeQuestHandler(GameQuestStore store, GameQuestSessionRegistry registry)
    : IZLinkSessionPacketHandler<IZLinkSessionContext>
{
    public string PacketName => nameof(SubscribeQuestReq);

    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        _ = header;
        var request = payload.Decode<SubscribeQuestReq>();
        await store.BindSessionAsync(registry.Bind(request.PlayerId, context), cancellationToken);
        var projection = await store.ReadProjectionAsync(request.PlayerId, cancellationToken);
        await context.Client.Reply(new SubscribeQuestRes(projection)).Async();
    }
}

internal sealed class GetQuestProgressHandler(GameQuestStore store)
    : IZLinkSessionPacketHandler<IZLinkSessionContext>
{
    public string PacketName => nameof(GetQuestProgressReq);

    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        _ = header;
        var request = payload.Decode<GetQuestProgressReq>();
        await context.Client.Reply(new GetQuestProgressRes(
                await store.ReadProjectionAsync(request.PlayerId, cancellationToken)))
            .Async();
    }
}

internal sealed class SyncQuestProgressHandler(GameplayActionService actions)
    : IZLinkSessionPacketHandler<IZLinkSessionContext>
{
    public string PacketName => nameof(SyncQuestProgressReq);

    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        _ = header;
        var request = payload.Decode<SyncQuestProgressReq>();
        await context.Client.Reply(await actions.SyncAsync(request.PlayerId, cancellationToken))
            .Async();
    }
}
