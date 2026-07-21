using Bingo.Server.Configuration;
using Bingo.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Systems.Zlink;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;

namespace Bingo.Server.Session.Sessions.Handlers;

internal sealed class AuthenticateBingoSessionHandler(
    IZLinkRouteClient channels,
    IZLinkSpotClient spotsClient,
    IZLinkSpotHandleResolver spots,
    IZLinkAllocatedRoutingIdProvider allocatedRoutingIds,
    ILogger<AuthenticateBingoSessionHandler> logger)
    : IZLinkSessionPacketHandler<IZLinkSessionContext, AuthenticateReq>
{
    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        AuthenticateReq request,
        CancellationToken cancellationToken)
    {
        var authenticated = await channels.RequestToChannel(SampleNames.MeshName, SampleNames.ApiChannel,
                new AuthenticatePlayerReq { AccessToken = request.AccessToken })
            .Async<AuthenticatePlayerRes>(cancellationToken);

        if (!authenticated.Accepted
            || string.IsNullOrWhiteSpace(authenticated.ActorId)
            || string.IsNullOrWhiteSpace(authenticated.DisplayName))
            throw new InvalidOperationException(authenticated.Reason ?? "Player authentication failed.");

        var sessionAllocation = await allocatedRoutingIds.WaitForReadyAllocationAsync(
            "bingo.session",
            cancellationToken);
        var preferredPlayNodeRid = RoutingId.From($"play{sessionAllocation.Slot}");
        var playEntrySpot = await spots.ResolveSpotHandleAsync(
                                SampleNames.MeshName,
                                preferredPlayNodeRid,
                                cancellationToken)
                            ?? throw new InvalidOperationException(
                                $"Play entry spot '{preferredPlayNodeRid}' was not found.");
        var ensured = await spotsClient.RequestToSpot(playEntrySpot,
                new EnsurePlayerActorReq
                {
                    ActorId = authenticated.ActorId,
                    DisplayName = authenticated.DisplayName,
                    PreferredActorNodeRid = preferredPlayNodeRid.ToString()
                })
            .Async<EnsurePlayerActorRes>(cancellationToken);

        var boundActor = await context.Actors.BindOrGetAsync(
            ToActorRef(ensured.Actor),
            cancellationToken);
        logger.LogInformation(
            "bingo session: bound player={ActorId} node={NodeRid} session={SessionId}",
            boundActor.ActorId,
            ensured.Actor.NodeRid,
            context.SessionId);

        await context.Client.Reply(new AuthenticateRes
            {
                ActorId = ensured.ActorId,
                DisplayName = authenticated.DisplayName,
                ActorNodeRid = ensured.Actor.NodeRid
            })
            .SubmitAsync(cancellationToken);
    }

    private static ActorRef ToActorRef(ActorRefWire snapshot)
    {
        return new ActorRef(
            RoutingId.From(snapshot.NodeRid),
            snapshot.ActorId,
            snapshot.Generation);
    }
}
