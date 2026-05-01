using Systems.Zlink.Stream.Connector.Contracts;
using TicTacToe.SessionActorDispatch.Contracts;
using TicTacToe.SessionActorDispatch.Infrastructure;
using Zlink;
using Zlink.Codecs.Json;
using Zlink.Framework.Streams;

namespace TicTacToe.SessionActorDispatch.Session;

internal sealed class JoinMatchSessionPacketHandler(
    RegistryPlayRouteStore routes,
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
        var request = payload.FromJson<JoinMatchReq>();
        var route = await routes.ResolveMatchAsync(request.MatchId, cancellationToken)
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
