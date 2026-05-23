using Bingo.Shared.Configuration;
using Bingo.Shared.Contracts;
using Systems.Zlink;
using Systems.Zlink.Stream.Connector.Contracts;

namespace Bingo.Server.Session.Sessions.Handlers;

internal sealed class AuthenticateBingoSessionHandler(IZLinkClient channels)
    : IZLinkSessionPacketHandler<IZLinkSessionContext>
{
    public string PacketName => nameof(AuthenticateReq);

    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        _ = header;
        var request = payload.Decode<AuthenticateReq>();
        var authenticated = await channels.Request(
                SampleNames.ApiChannel,
                new AuthenticatePlayerReq(request.AccessToken))
            .Timeout(SampleTimings.RequestTimeout)
            .SubmitAsync<AuthenticatePlayerRes>(cancellationToken)
            ;
        if (!authenticated.Accepted
            || string.IsNullOrWhiteSpace(authenticated.ActorId)
            || string.IsNullOrWhiteSpace(authenticated.DisplayName))
        {
            throw new InvalidOperationException(authenticated.Reason ?? "Player authentication failed.");
        }

        var ensured = await channels.Request(
                SampleNames.PlayChannel,
                new EnsurePlayerActorReq(authenticated.ActorId, authenticated.DisplayName))
            .Timeout(SampleTimings.RequestTimeout)
            .SubmitAsync<EnsurePlayerActorRes>(cancellationToken) ;

        await context.BindActorHandleAsync(
                ensured.ActorId,
                ensured.ActorType,
                ToRemoteAddress(ensured.RemoteAddress),
                cancellationToken)
            ;

        await context.Reply(new AuthenticateRes(ensured.ActorId, authenticated.DisplayName))
            .Submit(cancellationToken)
            ;
    }

    private static ZLinkActorRemoteAddress ToRemoteAddress(ActorRemoteAddressSnapshot snapshot)
    {
        return new ZLinkActorRemoteAddress(
            snapshot.RouterChannelId,
            RoutingId.FromBytes(snapshot.TargetNodeRid),
            snapshot.ActorGeneration);
    }
}
