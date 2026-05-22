using Systems.Zlink;
using Systems.Zlink.Stream.Connector.Contracts;
using TicTacToe.SessionGateway.Shared.Configuration;
using TicTacToe.SessionGateway.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;

namespace TicTacToe.SessionActorDispatch.Session;

internal sealed class AuthenticateSessionPacketHandler(IZLinkClient channels) : ISessionRelayPacketHandler
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
            var authenticated = await channels.Request(
                    SampleNames.ApiChannel,
                    new AuthenticateActorReq(request.ActorId))
                .Timeout(SampleTimings.RequestTimeout)
                .SubmitAsync<AuthenticateActorRes>(cancellationToken)
                ;
            if (!authenticated.Accepted || string.IsNullOrWhiteSpace(authenticated.ActorId))
            {
                throw new InvalidOperationException(authenticated.Reason ?? "Actor authentication failed.");
            }

            var ensured = await channels.Request(
                    SampleNames.PlayChannel,
                    new EnsurePlayerActorReq(authenticated.ActorId))
                .Timeout(SampleTimings.RequestTimeout)
                .SubmitAsync<EnsurePlayerActorRes>(cancellationToken)
                ;

            var actor = await context.Stream.BindActorHandleAsync(
                    ensured.ActorId,
                    SampleNames.PlayerActorType,
                    ToRemoteAddress(ensured.RemoteAddress),
                    cancellationToken)
                ;

            context.State.AttachAuthenticatedActor(actor);

            await context.Stream.Reply(new AuthenticateRes(ensured.ActorId))
                .Submit(cancellationToken)
                ;
        }
    }

    private static ZLinkActorRemoteAddress ToRemoteAddress(ActorRemoteAddressSnapshot snapshot)
    {
        return new ZLinkActorRemoteAddress(
            snapshot.RouterChannelId,
            RoutingId.FromBytes(snapshot.TargetNodeRid),
            snapshot.ActorGeneration);
    }
}
