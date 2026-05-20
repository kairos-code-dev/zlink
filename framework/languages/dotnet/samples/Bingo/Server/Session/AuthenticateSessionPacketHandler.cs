using Bingo.Shared.Configuration;
using Bingo.Shared.Contracts;
using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.Contracts.Actors;

namespace Bingo.Server.Session;

internal sealed class AuthenticateSessionPacketHandler(
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
            var ensured = await context.Stream.RequestChannel(
                    SampleNames.PlayChannel,
                    new EnsurePlayerActorReq(authenticated.ActorId, authenticated.DisplayName))
                .Timeout(SampleTimings.RequestTimeout)
                .SubmitAsync<EnsurePlayerActorRes>(cancellationToken)
                .ConfigureAwait(false);

            var route = new ZLinkActorRoute(
                ensured.Route.RouterChannelId,
                RoutingId.FromBytes(ensured.Route.TargetNodeRid));
            context.State.ActorId = ensured.ActorId;
            await actorRoutes.EnsureRouteAsync(context.Stream, context.State, route, cancellationToken)
                .ConfigureAwait(false);

            await context.Stream.Reply(new AuthenticateRes(ensured.ActorId, authenticated.DisplayName))
                .Submit(cancellationToken)
                .ConfigureAwait(false);
        }
    }
}
