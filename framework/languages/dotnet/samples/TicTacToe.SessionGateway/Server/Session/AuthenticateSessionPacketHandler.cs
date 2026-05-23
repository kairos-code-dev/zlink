using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using Systems.Zlink.Stream.Connector.Contracts;
using TicTacToe.SessionGateway.Shared.Configuration;
using TicTacToe.SessionGateway.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Streams;

namespace TicTacToe.SessionActorDispatch.Session;

internal sealed class AuthenticateSessionPacketHandler(IZLinkClient channels)
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

        await context.BindActorHandleAsync(
                ensured.ActorId,
                ensured.ActorType,
                ToRemoteAddress(ensured.RemoteAddress),
                cancellationToken)
            ;

        await context.Reply(new AuthenticateRes(ensured.ActorId))
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
