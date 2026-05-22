using Systems.Zlink;
using Systems.Zlink.Stream.Connector.Contracts;
using TicTacToe.SessionGateway.Shared.Contracts;

namespace TicTacToe.SessionActorDispatch.Session;

internal sealed class PlaceMarkSessionPacketHandler : ISessionRelayPacketHandler
{
    public string PacketName => nameof(PlaceMarkReq);

    public async ValueTask HandleAsync(
        SessionRelayPacketContext context,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        var actor = await context.State.RequireActorAsync(
                context.Stream,
                "sending game packets",
                cancellationToken)
            .ConfigureAwait(false);
        await context.Stream.RelayToActorAsync(actor, header, payload, cancellationToken)
            .ConfigureAwait(false);
    }
}
