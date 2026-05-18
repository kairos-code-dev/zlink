using Systems.Zlink;
using Systems.Zlink.Stream.Connector.Contracts;
using TicTacToe.Server.Play.Actors;

namespace TicTacToe.Server.Play.Sessions;

sealed class PlaySession(
    PlaySessionAuthenticator authenticator,
    ILogger<PlaySession> logger)
    : IZLinkSession
{
    private string? _actorId;
    private IZLinkActorRef? _actor;

    public IZLinkSessionContext Context { get; set; } = null!;

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
        var actorId = _actorId;
        var actor = _actor;
        _actorId = null;
        _actor = null;
        logger.LogInformation(
            "client -> play stream: disconnected. sessionId={SessionId}, actor={ActorId}",
            Context.SessionId,
            actorId ?? "(unauthenticated)");

        if (actor is not null)
        {
            await actor.NotifyDisconnectedAsync(cancellationToken).ConfigureAwait(false);
        }
    }

    public ValueTask OnErrorAsync(
        ZLinkStreamError error,
        CancellationToken cancellationToken)
    {
        logger.LogWarning(
            "play stream: error. code={Code}, message={Message}, actor={ActorId}",
            error.Error,
            error.Diagnostic?.Message,
            _actorId ?? "(unauthenticated)");
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }

    public async ValueTask OnDispatchAsync(
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        var actorId = _actorId;

        logger.LogInformation(
            "client -> play stream: message received. name={MessageName}, kind={Kind}, actor={ActorId}",
            header.Name,
            header.Kind,
            actorId ?? "(unauthenticated)");

        if (string.Equals(header.Name, nameof(AuthenticateReq), StringComparison.Ordinal))
        {
            var authenticated = await authenticator.AuthenticateAsync(Context, payload, cancellationToken);
            _actorId = authenticated.ActorId;
            _actor = authenticated.Actor;
            return;
        }

        if (actorId is null || _actor is not { } actor)
        {
            throw new InvalidOperationException("AuthenticateReq is required before play packets.");
        }

        logger.LogInformation(
            "play stream -> actor: dispatching packet. name={MessageName}, actor={ActorId}",
            header.Name,
            actorId);
        await Context.DispatchToActorAsync(actor, header, payload, cancellationToken);
    }
}
