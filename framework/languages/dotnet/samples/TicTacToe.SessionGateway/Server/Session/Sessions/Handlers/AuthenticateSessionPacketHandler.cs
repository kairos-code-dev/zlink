using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using Systems.Zlink.Stream.Connector.Contracts;
using TicTacToe.SessionGateway.Shared.Configuration;
using TicTacToe.SessionGateway.Shared.Contracts;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Streams;

namespace TicTacToe.SessionGateway.Session.Sessions.Handlers;

internal sealed class AuthenticateSessionPacketHandler(
    IZLinkChannelClient channels)
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
        var authenticated = await channels.RequestToChannel(
                SampleNames.ApiChannel,
                new AuthenticateActorReq(request.ActorId))
            .Timeout(SampleTimings.RequestTimeout)
            .SubmitAsync<AuthenticateActorRes>(cancellationToken);
        if (!authenticated.Accepted || string.IsNullOrWhiteSpace(authenticated.ActorId))
        {
            throw new InvalidOperationException(authenticated.Reason ?? "Actor authentication failed.");
        }

        var ensured = await channels.RequestToChannel(
                SampleNames.PlayChannel,
                new EnsurePlayerActorReq(authenticated.ActorId))
            .Timeout(SampleTimings.RequestTimeout)
            .SubmitAsync<EnsurePlayerActorRes>(cancellationToken)
            ;

        await context.Actors.BindAsync(
                ToActorRef(ensured.Actor),
                cancellationToken);

        await context.Client.Reply(new AuthenticateRes(ensured.ActorId))
            .Submit(cancellationToken);
    }

    private static ActorRef ToActorRef(ActorRefSnapshot snapshot)
    {
        return new ActorRef(
            RoutingId.From(snapshot.NodeRid),
            snapshot.ActorId,
            snapshot.Generation);
    }
}
