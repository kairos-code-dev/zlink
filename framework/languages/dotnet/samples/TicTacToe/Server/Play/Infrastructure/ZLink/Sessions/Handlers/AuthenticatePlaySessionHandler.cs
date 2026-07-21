using TicTacToe.Server.Configuration;
using TicTacToe.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Streams;

namespace TicTacToe.Server.Play.Infrastructure.ZLink.Sessions.Handlers;

internal sealed class AuthenticatePlaySessionHandler(
    IZLinkActorManager actors,
    IZLinkRouteClient channels,
    ILogger<AuthenticatePlaySessionHandler> logger)
    : IZLinkSessionPacketHandler<IZLinkSessionContext, AuthenticateReq>
{
    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        AuthenticateReq authenticate,
        CancellationToken cancellationToken)
    {
        logger.LogInformation(
            "play stream: authenticate requested. sessionId={SessionId}",
            context.SessionId);

        var accessToken = authenticate.AccessToken.Trim();
        if (string.IsNullOrWhiteSpace(accessToken))
            throw new InvalidOperationException("Authentication token is empty.");

        var authenticated = await channels.RequestToChannel(
                SampleNodes.Mesh,
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
            .SubmitAsync(cancellationToken);
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
        var playerActor = await actors.GetOrCreateAsync(
            player.ActorId,
            SampleTypes.PlayerActor,
            player,
            cancellationToken);
        logger.LogInformation(
            "play stream: binding actor to session. sessionId={SessionId}, actor={ActorId}",
            context.SessionId,
            player.ActorId);
        var boundActor = await context.Actors.BindOrGetAsync(
            playerActor,
            cancellationToken);

        logger.LogInformation(
            "play stream: actor bound to session. sessionId={SessionId}, actor={ActorId}",
            context.SessionId,
            boundActor.ActorId);
    }
}
