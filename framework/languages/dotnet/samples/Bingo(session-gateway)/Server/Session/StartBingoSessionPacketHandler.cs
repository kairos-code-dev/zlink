using Bingo.SessionGateway.Shared.Contracts;
using Systems.Zlink.Stream.Connector.Contracts;

namespace Bingo.SessionGateway.Session;

internal sealed class StartBingoSessionPacketHandler(
    IZLinkActorPlayRouteResolver playRoutes,
    SessionActorRouteCache actorRoutes)
    : ISessionRelayPacketHandler
{
    public string PacketName => nameof(StartBingoGameReq);

    public async ValueTask HandleAsync(
        SessionRelayPacketContext context,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        var actorId = context.State.RequireActorId("starting bingo");
        var route = await playRoutes.ResolvePlayRouteAsync(actorId, cancellationToken)
            .ConfigureAwait(false);
        var actor = await actorRoutes.EnsureRouteAsync(context.Stream, context.State, route, cancellationToken)
            .ConfigureAwait(false);
        await context.Stream.RelayToActorAsync(actor, header, payload, cancellationToken)
            .ConfigureAwait(false);
    }
}
