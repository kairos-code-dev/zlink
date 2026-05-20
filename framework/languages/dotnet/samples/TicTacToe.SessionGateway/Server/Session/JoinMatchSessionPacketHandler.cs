using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using TicTacToe.SessionGateway.Shared.Contracts;
using Zlink.Framework.Contracts.Streams;

namespace TicTacToe.SessionActorDispatch.Session;

internal sealed class JoinMatchSessionPacketHandler(
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
        var actor = await actorRoutes.EnsureRouteAsync(
                context.Stream,
                context.State,
                cancellationToken)
            .ConfigureAwait(false);
        await context.Stream.RelayToActorAsync(actor, header, payload, cancellationToken)
            .ConfigureAwait(false);
    }
}
