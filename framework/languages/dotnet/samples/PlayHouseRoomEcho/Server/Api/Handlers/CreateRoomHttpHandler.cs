namespace PlayHouseRoomEcho.Server.Api.Handlers;

internal static class CreateRoomHttpHandler
{
    public static async Task<IResult> HandleAsync(
        CreateRoomHttpRequest request,
        IZLinkClient client,
        ILoggerFactory loggerFactory,
        CancellationToken cancellationToken)
    {
        var logger = loggerFactory.CreateLogger("Game.Api.CreateRoom");
        var roomName = request.RoomName ?? "sample-room";
        logger.LogInformation("client -> api: create room requested. room={RoomName}", roomName);
        logger.LogInformation("api -> play: requesting CreateRoomCommand. room={RoomName}", roomName);

        var reply = await client.Request(
                "Play",
                new CreateRoomCommand(roomName))
            .WithTimeout(TimeSpan.FromSeconds(5))
            .ExecAsync(cancellationToken)
            .ConfigureAwait(false);

        logger.LogInformation(
            "play -> api: room created. roomId={RoomId}, endpoint={Endpoint}, room={RoomName}",
            reply.RoomId,
            reply.PlayEndpoint,
            reply.RoomName);
        logger.LogInformation(
            "api -> client: returning room info. roomId={RoomId}, endpoint={Endpoint}",
            reply.RoomId,
            reply.PlayEndpoint);

        return Results.Ok(new CreateRoomHttpReply(
            reply.RoomId,
            reply.PlayEndpoint,
            reply.RoomName));
    }
}
