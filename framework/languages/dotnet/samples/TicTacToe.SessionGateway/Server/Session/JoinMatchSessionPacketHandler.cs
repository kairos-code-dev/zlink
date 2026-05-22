using Systems.Zlink;
using Systems.Zlink.Stream.Connector.Contracts;
using TicTacToe.SessionGateway.Shared.Contracts;

namespace TicTacToe.SessionActorDispatch.Session;

internal sealed class JoinMatchSessionPacketHandler : ISessionRelayPacketHandler
{
    public string PacketName => nameof(JoinMatchReq);

    public async ValueTask HandleAsync(
        SessionRelayPacketContext context,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        var request = SessionRelayJson.Decode<JoinMatchReq>(payload);
        var actor = await context.State.RequireActorAsync(
                context.Stream,
                "joining a match",
                cancellationToken)
            .ConfigureAwait(false);
        await context.Stream.RelayToActorAsync(actor, header, payload, cancellationToken)
            .ConfigureAwait(false);
    }
}
