using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Streams;

namespace SupportChat.Server.Session.Sessions;

internal sealed class SupportChatSession(
    IZLinkSessionContext context,
    IZLinkSessionPacketDispatcher<IZLinkSessionContext> handlers) : IZLinkSession
{
    public IZLinkSessionContext Context { get; } = context;

    public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }

    public ValueTask OnErrorAsync(
        ZLinkStreamError error,
        CancellationToken cancellationToken)
    {
        _ = error;
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }

    public async ValueTask OnDispatchAsync(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        if (await handlers.TryHandleAsync(
                Context,
                dispatch,
                payload,
                cancellationToken))
            return;

        cancellationToken.ThrowIfCancellationRequested();
        var actor = RequireSingleBoundActor($"relaying packet '{dispatch.PacketName}'");
        await actor.RelayAsync(
            payload,
            cancellationToken);
    }

    private IZLinkSessionActor RequireSingleBoundActor(string action)
    {
        var actors = Context.Actors.Bound;
        return actors.Count switch
        {
            1 => actors.Single(),
            0 => throw new InvalidOperationException($"Client must authenticate before {action}."),
            _ => throw new InvalidOperationException($"Exactly one actor must be bound before {action}.")
        };
    }
}