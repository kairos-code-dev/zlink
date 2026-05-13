using Systems.Zlink.Stream.Connector.Contracts;
using TicTacToe.SessionActorDispatch.Infrastructure;
using TicTacToe.SessionGateway.Infrastructure;
using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using TicTacToe.SessionGateway.Shared.Contracts;
using Zlink.Framework.Streams;

namespace TicTacToe.SessionActorDispatch.Session;

internal sealed class JoinMatchSessionPacketHandler(
    ISpotRouteResolver spotRoutes,
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
        var route = await spotRoutes.ResolveSpotRouteAsync(spotRid, cancellationToken)
            .ConfigureAwait(false);
        var actor = await actorRoutes.EnsureRouteAsync(
                context.Stream,
                context.State,
                route,
                cancellationToken)
            .ConfigureAwait(false);
        await context.Stream.DispatchToActorAsync(actor, header, payload, cancellationToken)
            .ConfigureAwait(false);
    }
}
