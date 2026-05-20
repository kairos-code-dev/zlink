using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink;
using TicTacToe.SessionGateway.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Streams;

namespace TicTacToe.SessionActorDispatch.Session;

internal sealed class PlaceMarkSessionPacketHandler(SessionActorRouteCache actorRoutes)
    : ISessionRelayPacketHandler
{
    public string PacketName => nameof(PlaceMarkReq);

    public async ValueTask HandleAsync(
        SessionRelayPacketContext context,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        _ = context.State.RequireActorId("sending game packets");
        var actor = await actorRoutes.EnsureRouteAsync(
                context.Stream,
                context.State,
                cancellationToken)
            .ConfigureAwait(false);
        await context.Stream.RelayToActorAsync(actor, header, payload, cancellationToken)
            .ConfigureAwait(false);
    }
}
