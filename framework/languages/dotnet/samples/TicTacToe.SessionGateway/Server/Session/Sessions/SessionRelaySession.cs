using Systems.Zlink;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.Contracts.Streams;

namespace TicTacToe.SessionGateway.Session.Sessions;

internal sealed class SessionRelaySession(
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
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        if (await handlers.TryHandleAsync(
                Context,
                header,
                payload,
                cancellationToken))
        {
            return;
        }

        cancellationToken.ThrowIfCancellationRequested();
        var actor = RequireSingleBoundActor($"relaying packet '{header.Name}'");
        await actor.RelayAsync(
                header,
                payload,
                cancellationToken)
            ;
    }

    private IZLinkSessionActor RequireSingleBoundActor(string action)
    {
        var actors = Context.BoundActors;
        return actors.Count switch
        {
            1 => actors.Single(),
            0 => throw new InvalidOperationException($"Client must authenticate before {action}."),
            _ => throw new InvalidOperationException($"Exactly one actor must be bound before {action}.")
        };
    }
}
