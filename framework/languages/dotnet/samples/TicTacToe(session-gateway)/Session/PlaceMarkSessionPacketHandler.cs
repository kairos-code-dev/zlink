using Systems.Zlink.Stream.Connector.Contracts;
using TicTacToe.SessionActorDispatch.Contracts;
using TicTacToe.SessionActorDispatch.Infrastructure;
using Zlink;
using Zlink.Framework.Streams;

namespace TicTacToe.SessionActorDispatch.Session;

internal sealed class PlaceMarkSessionPacketHandler(
    RegistryPlayRouteStore routes,
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
        var route = await routes.ResolveActorPlayAsync(actorId, cancellationToken)
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
