using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Streams;
using Systems.Zlink;
using Systems.Zlink.Stream.Connector.Contracts;

namespace TicTacToe.Server.Play.Infrastructure.ZLink.Sessions;

sealed class PlaySession(
    IZLinkSessionContext context,
    IZLinkSessionPacketDispatcher<IZLinkSessionContext> handlers,
    ILogger<PlaySession> logger)
    : IZLinkSession
{
    public IZLinkSessionContext Context { get; } = context;

    public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        logger.LogInformation(
            "client -> play stream: connected. sessionId={SessionId}",
            Context.SessionId);
        return ValueTask.CompletedTask;
    }

    public async ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
    {
        var boundActors = Context.Actors.Bound.ToArray();
        logger.LogInformation(
            "client -> play stream: disconnected. sessionId={SessionId}, actors={ActorCount}",
            Context.SessionId,
            boundActors.Length);

        foreach (var actor in boundActors)
        {
            await actor.NotifyDisconnectedAsync(cancellationToken);
        }
    }

    public ValueTask OnErrorAsync(
        ZLinkStreamError error,
        CancellationToken cancellationToken)
    {
        logger.LogWarning(
            "play stream: error. code={Code}, message={Message}, sessionId={SessionId}",
            error.Error,
            error.Diagnostic?.Message,
            Context.SessionId);
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }

    public async ValueTask OnDispatchAsync(
        ZlinkStreamHeader header,
        Zlink.Framework.Contracts.Messaging.ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        logger.LogInformation(
            "client -> play stream: message received. name={MessageName}, kind={Kind}, sessionId={SessionId}",
            header.Name,
            header.Kind,
            Context.SessionId);

        if (await handlers.TryHandleAsync(
                Context,
                header,
                payload,
                cancellationToken))
        {
            return;
        }

        var actor = RequireSingleBoundActor($"relaying packet '{header.Name}'");
        await actor.RelayAsync(header, payload, cancellationToken);
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
