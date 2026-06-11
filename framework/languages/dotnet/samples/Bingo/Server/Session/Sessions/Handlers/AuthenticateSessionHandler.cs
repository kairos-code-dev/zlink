using Systems.Zlink;
using Systems.Zlink.Codecs.Protobuf;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Streams;
using Bingo.Shared.Configuration;
using Bingo.Shared.Contracts;
using Systems.Zlink.Stream.Connector.Contracts;

namespace Bingo.Server.Session.Sessions.Handlers;

internal sealed class AuthenticateBingoSessionHandler(IZLinkChannelClient channels)
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
        var request = payload.FromProto<AuthenticateReq>();
        var authenticated = await channels.RequestToChannel(
                SampleNames.ApiChannel,
                new AuthenticatePlayerReq { AccessToken = request.AccessToken })
            .Async<AuthenticatePlayerRes>(cancellationToken);

        if (!authenticated.Accepted
            || string.IsNullOrWhiteSpace(authenticated.ActorId)
            || string.IsNullOrWhiteSpace(authenticated.DisplayName))
        {
            throw new InvalidOperationException(authenticated.Reason ?? "Player authentication failed.");
        }

        var ensured = await channels.RequestToChannel(
                SampleNames.PlayChannel,
                new EnsurePlayerActorReq
                {
                    ActorId = authenticated.ActorId,
                    DisplayName = authenticated.DisplayName,
                })
            .Async<EnsurePlayerActorRes>(cancellationToken) ;

        await context.Actors.BindAsync(
                ToActorRef(ensured.Actor),
                cancellationToken);

        await context.Client.Reply(new AuthenticateRes
            {
                ActorId = ensured.ActorId,
                DisplayName = authenticated.DisplayName,
            })
            .Async();
    }

    private static ActorRef ToActorRef(ActorRefSnapshot snapshot)
    {
        return new ActorRef(
            RoutingId.FromHex(snapshot.NodeRid),
            snapshot.ActorId,
            snapshot.Generation);
    }
}
