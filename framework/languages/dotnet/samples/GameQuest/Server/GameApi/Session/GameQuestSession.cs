using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.Contracts.Streams;

namespace GameQuest.GameApi.Session;

internal sealed class GameQuestSession(
    IZLinkSessionContext context,
    IZLinkSessionPacketDispatcher<IZLinkSessionContext> handlers,
    GameQuestSessionRegistry registry,
    Adapters.Store.GameQuestStore store) : IZLinkSession
{
    public IZLinkSessionContext Context { get; } = context;

    public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }

    public async ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
    {
        foreach (var unbind in registry.Remove(Context))
        {
            await store.UnbindSessionAsync(unbind, cancellationToken);
        }
    }

    public ValueTask OnErrorAsync(ZLinkStreamError error, CancellationToken cancellationToken)
    {
        _ = error;
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }

    public async ValueTask OnDispatchAsync(
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        if (!await handlers.TryHandleAsync(Context, header, payload, cancellationToken))
        {
            throw new InvalidOperationException($"Unsupported GameQuest packet '{header.Name}'.");
        }
    }
}
