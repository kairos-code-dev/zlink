using Systems.Zlink.Codecs.Json;
using TicTacToe.Server.Configuration;
using TicTacToe.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Streams;
using System.Text.Json;
using Systems.Zlink;
using Systems.Zlink.Stream.Connector.Contracts;

namespace TicTacToe.Server.Play.Sessions;

sealed class PlaySession(
    IZLinkSessionContext context,
    IZLinkActorManager actors,
    IZLinkChannelClient channels,
    ILogger<PlaySession> logger)
    : IZLinkSession
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);

    private string? _actorId;
    private IZLinkSessionActor? _actor;

    public IZLinkSessionContext Context { get; } = context;

    public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        logger.LogInformation(
            "client -> play stream: connected. sessionId={SessionId}",
            Context.SessionId);
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
    {
        var actorId = _actorId;
        _actorId = null;
        _actor = null;
        logger.LogInformation(
            "client -> play stream: disconnected. sessionId={SessionId}, actor={ActorId}",
            Context.SessionId,
            actorId ?? "(unauthenticated)");

        _ = cancellationToken;
        return ValueTask.CompletedTask;
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
            var authenticated = await AuthenticateAsync(payload, cancellationToken);
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
        await actor.RelayAsync(header, payload, cancellationToken);
    }

    private async ValueTask<AuthenticatedPlaySession> AuthenticateAsync(
        Message payload,
        CancellationToken cancellationToken)
    {
        var authenticate = payload.Decode<AuthenticateReq>(JsonOptions);

        logger.LogInformation(
            "play stream -> api: authenticate requested. sessionId={SessionId}",
            Context.SessionId);

        var reply = await channels.RequestToChannel(
                SampleChannels.Api,
                new AuthenticatePlayerReq(authenticate.AccessToken))
            .Timeout(SampleTimeouts.Request)
            .SubmitAsync<AuthenticatePlayerRes>(cancellationToken);

        var playerActor = await actors.GetOrCreateAsync(
            reply.ActorId,
            SampleTypes.PlayerActor,
            cancellationToken);

        var actor = await Context.Actors.BindAsync(
            playerActor,
            cancellationToken);
        await Context.Client.Reply(new AuthenticateRes(reply.ActorId))
            .Submit();

        logger.LogInformation(
            "api -> play stream: authenticate accepted. sessionId={SessionId}, player={ActorId}, actor={ActorId}",
            Context.SessionId,
            reply.ActorId,
            actor.ActorId);

        return new AuthenticatedPlaySession(reply.ActorId, actor);
    }

    private readonly record struct AuthenticatedPlaySession(
        string ActorId,
        IZLinkSessionActor Actor);
}
