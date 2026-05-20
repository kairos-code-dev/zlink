using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using TicTacToe.SessionGateway.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;

namespace TicTacToe.SessionActorDispatch.Session;

internal sealed class JoinMatchSessionPacketHandler(
    IZLinkSpotRouteResolver spotRoutes,
    SessionActorRouteCache actorRoutes)
    : ISessionRelayPacketHandler
{
    public string PacketName => nameof(JoinMatchReq);

    public async ValueTask HandleAsync(
        SessionRelayPacketContext context,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        var request = SessionRelayJson.Decode<JoinMatchReq>(payload);
        var spotRid = RoutingId.FromString(request.MatchId);
        var spotRoute = await spotRoutes.ResolveSpotRouteAsync(spotRid, cancellationToken)
            .ConfigureAwait(false);
        var route = new ZLinkActorRoute(
            spotRoute.RouterChannelId,
            spotRoute.TargetNodeRid);
        var actor = await actorRoutes.EnsureRouteAsync(
                context.Stream,
                context.State,
                route,
                cancellationToken)
            .ConfigureAwait(false);
        await context.Stream.RelayToActorAsync(actor, header, payload, cancellationToken)
            .ConfigureAwait(false);
    }
}
