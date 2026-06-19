using TicTacToe.Server.Play.Adapters.ZLink.Actors;
using TicTacToe.Shared.Contracts;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;

namespace TicTacToe.Server.Play.Adapters.ZLink.Spots.Handlers;

internal sealed class PlayActorObserveMilestoneHandler(
    ILogger<PlayActorObserveMilestoneHandler> logger)
    : IZLinkEntrySpotActorRequestHandler<PlayEntrySpot, PlayActor, ObserveMilestoneReq, ObserveMilestoneRes>
{
    public async ValueTask<ObserveMilestoneRes> HandleAsync(
        PlayEntrySpot entrySpot,
        PlayActor actor,
        ZLinkSpotActorRequestContext context,
        ObserveMilestoneReq message,
        CancellationToken cancellationToken)
    {
        _ = context;
        _ = message;

        logger.LogInformation(
            "actor: ObserveMilestoneReq received. actor={ActorId}",
            actor.ActorId);

        await entrySpot.SubscribeMilestoneAsync(actor, cancellationToken);

        logger.LogInformation(
            "actor: ObserveMilestoneReq completed. actor={ActorId}, subscribed={Subscribed}",
            actor.ActorId,
            true);
        return new ObserveMilestoneRes(true);
    }
}
