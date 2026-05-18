using System.Text.Json;
using Systems.Zlink;

namespace TicTacToe.Server.Play.Sessions;

internal sealed class PlaySessionAuthenticator(
    ILogger<PlaySessionAuthenticator> logger)
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);

    public async ValueTask<AuthenticatedPlaySession> AuthenticateAsync(
        IZLinkSessionContext context,
        Message payload,
        CancellationToken cancellationToken)
    {
        var authenticate = payload.FromJson<AuthenticateReq>(JsonOptions);

        logger.LogInformation(
            "play stream -> api: authenticate requested. sessionId={SessionId}",
            context.SessionId);

        var reply = await context.RequestChannel(
                SampleChannels.Api,
                new AuthenticatePlayerReq(authenticate.AccessToken))
            .Timeout(SampleTimeouts.Request)
            .SubmitAsync<AuthenticatePlayerRes>(cancellationToken);

        var actor = await context.CreateAndBindActorAsync(
                reply.ActorId,
                SampleTypes.PlayerActor,
                cancellationToken)
            .ConfigureAwait(false);
        await context.Reply(new AuthenticateRes(reply.ActorId))
            .Submit(cancellationToken);

        logger.LogInformation(
            "api -> play stream: authenticate accepted. sessionId={SessionId}, player={ActorId}, actor={ActorId}",
            context.SessionId,
            reply.ActorId,
            actor.ActorId);

        return new AuthenticatedPlaySession(reply.ActorId, actor);
    }
}

internal readonly record struct AuthenticatedPlaySession(
    string ActorId,
    IZLinkActorRef Actor);
