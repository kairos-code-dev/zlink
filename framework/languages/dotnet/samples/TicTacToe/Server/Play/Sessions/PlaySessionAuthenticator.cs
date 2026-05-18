using System.Text.Json;
using Systems.Zlink;
using TicTacToe.Server.Play.Actors;

namespace TicTacToe.Server.Play.Sessions;

internal sealed class PlaySessionAuthenticator(
    PlayActorFactory actorFactory,
    ILogger<PlaySessionAuthenticator> logger)
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);

    public async ValueTask<PlayActor> AuthenticateAsync(
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

        var actor = (PlayActor)await actorFactory.CreateAsync(reply.ActorId, cancellationToken)
            .ConfigureAwait(false);
        await ((IZLinkSessionActorAttachmentContext)context).AttachActorAsync(actor, cancellationToken);
        await context.Reply(new AuthenticateRes(reply.ActorId))
            .Submit(cancellationToken);

        logger.LogInformation(
            "api -> play stream: authenticate accepted. sessionId={SessionId}, player={ActorId}, actor={ActorId}",
            context.SessionId,
            reply.ActorId,
            actor.ActorId);

        return actor;
    }
}
