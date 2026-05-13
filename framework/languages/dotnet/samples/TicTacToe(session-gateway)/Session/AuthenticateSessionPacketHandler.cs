using Systems.Zlink.Stream.Connector.Contracts;
using TicTacToe.SessionActorDispatch.Configuration;
using TicTacToe.SessionActorDispatch.Contracts;
using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using Zlink.Framework.Streams;

namespace TicTacToe.SessionActorDispatch.Session;

internal sealed class AuthenticateSessionPacketHandler(
    IZLinkActorPlayRouteResolver playRoutes,
    SessionActorRouteCache actorRoutes)
    : ISessionRelayPacketHandler
{
    public string PacketName => nameof(AuthenticateReq);

    public async ValueTask HandleAsync(
        SessionRelayPacketContext context,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        _ = header;
        using (payload)
        {
            var request = SessionRelayJson.Decode<AuthenticateReq>(payload);
            var authenticated = await context.Stream.RequestChannel(
                    SampleNames.ApiChannel,
                    new AuthenticateActorReq(request.ActorId))
                .WithTimeout(SampleTimings.RequestTimeout)
                .SubmitAsync<AuthenticateActorRes>(cancellationToken)
                .ConfigureAwait(false);
            if (!authenticated.Accepted || string.IsNullOrWhiteSpace(authenticated.ActorId))
            {
                throw new InvalidOperationException(authenticated.Reason ?? "Actor authentication failed.");
            }

            context.State.ActorId = authenticated.ActorId;
            var route = await playRoutes.ResolvePlayRouteAsync(authenticated.ActorId, cancellationToken)
                .ConfigureAwait(false);
            await actorRoutes.EnsureRouteAsync(
                    context.Stream,
                    context.State,
                    route,
                    cancellationToken)
                .ConfigureAwait(false);

            await context.Stream.Reply(new AuthenticateRes(authenticated.ActorId))
                .Submit(cancellationToken)
                .ConfigureAwait(false);
        }
    }
}
