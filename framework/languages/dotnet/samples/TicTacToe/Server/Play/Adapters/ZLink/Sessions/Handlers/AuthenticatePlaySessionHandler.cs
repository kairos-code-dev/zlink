using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using Systems.Zlink.Stream.Connector.Contracts;
using TicTacToe.Server.Configuration;
using TicTacToe.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Streams;

namespace TicTacToe.Server.Play.Adapters.ZLink.Sessions.Handlers;

internal sealed class AuthenticatePlaySessionHandler(
    IZLinkActorManager actors,
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

        try
        {
            var authenticate = payload.Decode<AuthenticateReq>();

            logger.LogInformation(
                "play stream: authenticate requested. sessionId={SessionId}",
                context.SessionId);

            var actorId = authenticate.AccessToken.Trim();
            if (string.IsNullOrWhiteSpace(actorId))
            {
                throw new InvalidOperationException("Authentication token is empty.");
            }

            await context.Client.Reply(new AuthenticateRes(actorId))
                .Async();

            logger.LogInformation(
                "play stream: authenticate accepted. sessionId={SessionId}, player={ActorId}",
                context.SessionId,
                actorId);

            await EnsureActorBoundAsync(
                context,
                actorId,
                cancellationToken);
        }
        catch (Exception ex)
        {
            logger.LogError(
                ex,
                "play stream: authenticate failed. sessionId={SessionId}",
                context.SessionId);
            throw;
        }
    }

    private async ValueTask EnsureActorBoundAsync(
        IZLinkSessionContext context,
        string actorId,
        CancellationToken cancellationToken)
    {
        logger.LogInformation(
            "play stream: creating actor before dispatch. sessionId={SessionId}, actor={ActorId}",
            context.SessionId,
            actorId);
        var playerActor = await actors.GetOrCreateAsync(
            actorId,
            SampleTypes.PlayerActor,
            cancellationToken);

        logger.LogInformation(
            "play stream: joining actor to entry spot. sessionId={SessionId}, actor={ActorId}",
            context.SessionId,
            actorId);
        await playerActor.Context.JoinEntrySpot(RoutingId.From(SampleTypes.PlaySpotNodeId))
            .Async(cancellationToken);

        logger.LogInformation(
            "play stream: binding actor to session. sessionId={SessionId}, actor={ActorId}",
            context.SessionId,
            actorId);
        var boundActor = await context.Actors.BindAsync(
            playerActor,
            cancellationToken);

        logger.LogInformation(
            "play stream: actor bound to session. sessionId={SessionId}, actor={ActorId}",
            context.SessionId,
            boundActor.ActorId);
    }
}
