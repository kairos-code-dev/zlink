using Zlink;
using TicTacToe.Server.Play.Actors;

namespace TicTacToe.Server.Play.Sessions;

internal sealed class PlaySessionAuthenticator(
    PlayActorFactory actorFactory,
    ILogger<PlaySessionAuthenticator> logger)
{
    public async ValueTask<PlayActor> AuthenticateAsync(
        IZLinkSessionContext context,
        Message body,
        CancellationToken cancellationToken)
    {
        var authenticate = body.FromJson<AuthenticateReq>();

        logger.LogInformation(
            "play stream -> api: authenticate requested. sessionId={SessionId}",
            context.SessionId);

        var reply = await context.RequestChannel(
                SampleChannels.Api,
                new AuthenticatePlayerReq(authenticate.AccessToken))
            .WithTimeout(SampleTimeouts.Request)
            .Async<AuthenticatePlayerRes>(cancellationToken);

        var actor = actorFactory.Create(reply.PlayerId);
        await context.AttachActorAsync(actor, cancellationToken);
        await context.Reply(new AuthenticateRes(reply.PlayerId))
            .Async(cancellationToken);

        logger.LogInformation(
            "api -> play stream: authenticate accepted. sessionId={SessionId}, player={PlayerId}, actor={ActorId}",
            context.SessionId,
            reply.PlayerId,
            actor.ActorId);

        return actor;
    }
}
