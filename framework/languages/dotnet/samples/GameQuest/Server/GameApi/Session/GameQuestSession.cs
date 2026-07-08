using GameQuest.GameApi.Infrastructure.Store;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Streams;

namespace GameQuest.GameApi.Session;

internal sealed class GameQuestSession(
    IZLinkSessionContext context,
    GameQuestSessionRegistry registry,
    GameQuestStore store) : IZLinkSession
{
    public IZLinkSessionContext Context { get; } = context;

    public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }

    public async ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
    {
        foreach (var unbind in registry.Remove(Context)) await store.UnbindSessionAsync(unbind, cancellationToken);
    }

    public ValueTask OnErrorAsync(ZLinkStreamError error, CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }

    public async ValueTask OnDispatchAsync(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        if (!await Context.Handlers.TryHandleAsync(dispatch, payload, cancellationToken))
            throw new InvalidOperationException($"Unsupported GameQuest packet '{dispatch.PacketName}'.");
    }
}
