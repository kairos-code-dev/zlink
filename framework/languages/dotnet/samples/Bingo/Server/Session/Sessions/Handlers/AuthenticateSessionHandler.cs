using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Timers;
using Bingo.Shared.Configuration;
using Bingo.Shared.Contracts;
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
                ToActorRef(ensured.Actor),
                ensured.ActorType,
                cancellationToken)
            ;

        await context.Reply(new AuthenticateRes(ensured.ActorId, authenticated.DisplayName))
            .Submit(cancellationToken)
            ;
    }

    private static ActorRef ToActorRef(ActorRefSnapshot snapshot)
    {
        return new ActorRef(
            RoutingId.FromBytes(snapshot.NodeRid),
            snapshot.ActorId,
            snapshot.Generation);
    }
}
