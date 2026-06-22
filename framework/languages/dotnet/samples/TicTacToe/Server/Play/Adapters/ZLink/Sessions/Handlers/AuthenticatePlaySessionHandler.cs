using Systems.Zlink;
using Zlink.Framework.Contracts.Codecs.Json;
using Systems.Zlink.Stream.Connector.Contracts;
using TicTacToe.Server.Configuration;
using TicTacToe.Server.Play.Adapters.ZLink.Actors;
using TicTacToe.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Streams;

namespace TicTacToe.Server.Play.Adapters.ZLink.Sessions.Handlers;

internal sealed class AuthenticatePlaySessionHandler(
    IZLinkActorManager actors,
    IZLinkChannelClient channels,
    SampleSettings settings,
    ILogger<AuthenticatePlaySessionHandler> logger)
    : IZLinkSessionPacketHandler<IZLinkSessionContext>
{
    public string PacketName => nameof(AuthenticateReq);

    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        _ = header;

        var authenticate = payload.Decode<AuthenticateReq>();

        logger.LogInformation(
            "play stream: authenticate requested. sessionId={SessionId}",
            context.SessionId);

        var accessToken = authenticate.AccessToken.Trim();
        if (string.IsNullOrWhiteSpace(accessToken))
        {
            throw new InvalidOperationException("Authentication token is empty.");
        }

        var authenticated = await channels.RequestToChannel(
                SampleChannels.Api,
                new AuthenticatePlayerReq(accessToken))
            .Async<AuthenticatePlayerRes>(cancellationToken);

        logger.LogInformation(
            "play stream: authenticate accepted. sessionId={SessionId}, player={ActorId}",
            context.SessionId,
            authenticated.Player.ActorId);

        await EnsureActorBoundAsync(
            context,
            authenticated.Player,
            cancellationToken);

        await context.Client.Reply(new AuthenticateRes(authenticated.Player))
            .Async();
    }

    private async ValueTask EnsureActorBoundAsync(
        IZLinkSessionContext context,
        PlayerInfo player,
        CancellationToken cancellationToken)
    {
        logger.LogInformation(
            "play stream: creating actor before dispatch. sessionId={SessionId}, actor={ActorId}",
            context.SessionId,
            player.ActorId);
        var playerActor = (PlayActor)await actors.GetOrCreateAsync(
                player.ActorId,
                SampleTypes.PlayerActor,
                cancellationToken);
        playerActor.ApplyPlayer(player);

        logger.LogInformation(
            "play stream: joining actor to entry spot. sessionId={SessionId}, actor={ActorId}",
            context.SessionId,
            player.ActorId);
        using var entryJoinRequest = Message.From(ReadOnlySpan<byte>.Empty);
        await playerActor.Context.JoinEntrySpot(
                RoutingId.From(settings.PlaySpotNodeRid),
                entryJoinRequest)
            .Async(cancellationToken);

        logger.LogInformation(
            "play stream: binding actor to session. sessionId={SessionId}, actor={ActorId}",
            context.SessionId,
            player.ActorId);
        var boundActor = await context.Actors.BindAsync(
            playerActor,
            cancellationToken);

        logger.LogInformation(
            "play stream: actor bound to session. sessionId={SessionId}, actor={ActorId}",
            context.SessionId,
            boundActor.ActorId);
    }
}
