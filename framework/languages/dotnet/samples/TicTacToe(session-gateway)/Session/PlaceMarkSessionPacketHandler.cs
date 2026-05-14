using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink;
using TicTacToe.SessionGateway.Shared.Contracts;
using Zlink.Framework.Contracts.Streams;

namespace TicTacToe.SessionActorDispatch.Session;

internal sealed class PlaceMarkSessionPacketHandler(
    IZLinkActorPlayRouteResolver playRoutes,
    SessionActorRouteCache actorRoutes)
    : ISessionRelayPacketHandler
{
    public string PacketName => nameof(PlaceMarkReq);

    public async ValueTask HandleAsync(
        SessionRelayPacketContext context,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        var actorId = context.State.RequireActorId("sending game packets");
        var route = await playRoutes.ResolvePlayRouteAsync(actorId, cancellationToken)
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
