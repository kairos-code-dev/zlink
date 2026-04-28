using Systems.Zlink.Stream.Connector.Abstractions;
using Zlink;
using PlayHouseRoomEcho.Server.Actors;

namespace PlayHouseRoomEcho.Server.Play.Handlers;

sealed class RoomStreamSession(
    IZLinkActorRuntime actorRuntime,
    ILogger<PlayerActor> actorLogger,
    ILogger<RoomStreamSession> logger)
    : IZLinkStreamHeaderSession
{
    private readonly PlayerActor _actor = new(Guid.NewGuid().ToString("N"), actorLogger);

    public ValueTask OnConnectedAsync(IZLinkStream stream, CancellationToken cancellationToken)
    {
        logger.LogInformation(
            "client -> play stream: connected. sessionId={SessionId}, actor={ActorKey}",
            stream.SessionId,
            _actor.ActorKey);
        return actorRuntime.AttachAsync(_actor, stream, cancellationToken);
    }

    public ValueTask OnDisconnectedAsync(IZLinkStream stream, CancellationToken cancellationToken)
    {
        logger.LogInformation(
            "client -> play stream: disconnected. sessionId={SessionId}, actor={ActorKey}",
            stream.SessionId,
            _actor.ActorKey);
        return actorRuntime.DisconnectAsync(_actor, stream, cancellationToken);
    }

    public ValueTask OnErrorAsync(
        IZLinkStream stream,
        ZLinkStreamError error,
        CancellationToken cancellationToken)
    {
        _ = stream;
        logger.LogWarning(
            "play stream: error. code={Code}, message={Message}, actor={ActorKey}",
            error.Error,
            error.Diagnostic?.Message,
            _actor.ActorKey);
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }

    public async ValueTask OnDispatchAsync(
        IZLinkStream stream,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        _ = stream;

        logger.LogInformation(
            "client -> play stream: packet received. name={PacketName}, kind={Kind}, actor={ActorKey}",
            header.Name,
            header.Kind,
            _actor.ActorKey);

        logger.LogInformation(
            "play stream -> actor runtime: dispatching packet. name={PacketName}, actor={ActorKey}",
            header.Name,
            _actor.ActorKey);
        await actorRuntime.SubmitAsync(
            _actor,
            header,
            body,
            cancellationToken);
    }
}
