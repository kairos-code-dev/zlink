using Bingo.Shared.Configuration;
using Bingo.Shared.Contracts;
using Systems.Zlink.Codecs.Json;
using Systems.Zlink.Stream.Connector.Contracts;

namespace Bingo.Server.Session;

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
                    new AuthenticatePlayerReq(request.AccessToken))
                .Timeout(SampleTimings.RequestTimeout)
                .SubmitAsync<AuthenticatePlayerRes>(cancellationToken)
                .ConfigureAwait(false);
            if (!authenticated.Accepted
                || string.IsNullOrWhiteSpace(authenticated.ActorId)
                || string.IsNullOrWhiteSpace(authenticated.DisplayName))
            {
                throw new InvalidOperationException(authenticated.Reason ?? "Player authentication failed.");
            }

            context.State.ActorId = authenticated.ActorId;
            await context.Stream.RequestChannel(
                    SampleNames.PlayChannel,
                    new EnsurePlayerActorReq(authenticated.ActorId, authenticated.DisplayName))
                .Timeout(SampleTimings.RequestTimeout)
                .SubmitAsync<EnsurePlayerActorRes>(cancellationToken)
                .ConfigureAwait(false);

            var route = await playRoutes.ResolvePlayRouteAsync(authenticated.ActorId, cancellationToken)
                .ConfigureAwait(false);
            await actorRoutes.EnsureRouteAsync(context.Stream, context.State, route, cancellationToken)
                .ConfigureAwait(false);

            await context.Stream.Reply(new AuthenticateRes(authenticated.ActorId, authenticated.DisplayName))
                .Submit(cancellationToken)
                .ConfigureAwait(false);
        }
    }
}
