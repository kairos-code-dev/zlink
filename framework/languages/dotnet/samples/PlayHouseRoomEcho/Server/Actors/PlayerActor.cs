using Zlink;
using Systems.Zlink.Stream.Connector.Abstractions;
using Systems.Zlink.Stream.Connector.Headers;

namespace PlayHouseRoomEcho.Server.Actors;

internal sealed class PlayerActor(
    string actorKey,
    ILogger<PlayerActor> logger)
    : IZLinkActor
{
    public string ActorKey { get; } = actorKey;

    public string RoomId { get; set; } = string.Empty;

    public string PlayerId { get; set; } = string.Empty;

    public ValueTask OnAttachedAsync(ZLinkSpot spot, CancellationToken cancellationToken)
    {
        _ = spot;
        _ = cancellationToken;
        logger.LogInformation(
            "play actor: attached to GameRoom. actor={ActorKey}, roomId={RoomId}",
            ActorKey,
            RoomId);
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDetachedAsync(ZLinkSpot spot, CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        logger.LogInformation(
            "play actor: detached from GameRoom. actor={ActorKey}, roomId={RoomId}",
            ActorKey,
            RoomId);
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        logger.LogInformation(
            "play actor: stream disconnected. actor={ActorKey}, roomId={RoomId}, player={PlayerId}",
            ActorKey,
            RoomId,
            PlayerId);
        return ValueTask.CompletedTask;
    }

    public async ValueTask OnDispatchAsync(
        IZLinkActorContext context,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        if (context.Client is null)
        {
            throw new InvalidOperationException("Actor has no active client stream.");
        }

        if (string.Equals(header.Name, nameof(JoinRoom), StringComparison.Ordinal))
        {
            var join = SampleStreamFrames.DecodeBody<JoinRoom>(body);
            logger.LogInformation(
                "actor: JoinRoom received. actor={ActorKey}, roomId={RoomId}, player={PlayerId}",
                ActorKey,
                join.RoomId,
                join.PlayerId);

            var spotRid = RoutingId.FromBytes(Convert.FromHexString(join.RoomId));
            var reply = await context.JoinSpotAsync<JoinRoomRequest, JoinRoomReply>(
                spotRid,
                new JoinRoomRequest(join.RoomId, join.PlayerId),
                cancellationToken);

            await context.Client.Reply(new JoinRoomAccepted(reply.RoomId, reply.PlayerId))
                .SendAsync(cancellationToken);
            logger.LogInformation(
                "actor -> client: JoinRoomAccepted sent. actor={ActorKey}, roomId={RoomId}, player={PlayerId}",
                ActorKey,
                reply.RoomId,
                reply.PlayerId);
            return;
        }

        if (context.Spot is null)
        {
            throw new InvalidOperationException("Actor has not joined a room.");
        }

        if (!string.Equals(header.Name, nameof(Echo), StringComparison.Ordinal))
        {
            return;
        }

        var echo = SampleStreamFrames.DecodeBody<Echo>(body);
        logger.LogInformation(
            "actor: Echo received. actor={ActorKey}, roomId={RoomId}, text={Text}",
            ActorKey,
            RoomId,
            echo.Text);
        await context.Client.Reply(new EchoReply(echo.Text)).SendAsync(cancellationToken);
        logger.LogInformation(
            "actor -> client: EchoReply sent. actor={ActorKey}, roomId={RoomId}, text={Text}",
            ActorKey,
            RoomId,
            echo.Text);
    }
}
