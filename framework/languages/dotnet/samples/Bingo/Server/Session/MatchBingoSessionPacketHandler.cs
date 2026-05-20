using Bingo.Shared.Contracts;
using Systems.Zlink.Stream.Connector.Contracts;

namespace Bingo.Server.Session;

internal sealed class MatchBingoSessionPacketHandler(SessionActorRouteCache actorRoutes)
    : ISessionRelayPacketHandler
{
    public string PacketName => nameof(MatchBingoReq);

    public async ValueTask HandleAsync(
        SessionRelayPacketContext context,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        _ = context.State.RequireActorId("matching bingo");
        var actor = await actorRoutes.EnsureRouteAsync(context.Stream, context.State, cancellationToken)
            .ConfigureAwait(false);
        await context.Stream.RelayToActorAsync(actor, header, payload, cancellationToken)
            .ConfigureAwait(false);
    }
}
