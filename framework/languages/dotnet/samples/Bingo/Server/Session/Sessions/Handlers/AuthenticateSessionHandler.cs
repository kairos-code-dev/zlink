using Bingo.Server.Configuration;
using Bingo.Shared.Contracts;
using Systems.Zlink;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Streams;

namespace Bingo.Server.Session.Sessions.Handlers;

internal sealed class AuthenticateBingoSessionHandler(
    IZLinkChannelClient channels,
    IZLinkRouteClient routes,
    SampleSessionNode session)
    : IZLinkSessionPacketHandler<IZLinkSessionContext>
{
    public string PacketName => nameof(AuthenticateReq);

    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        _ = dispatch;
        var request = payload.Decode<AuthenticateReq>();
        var authenticated = await channels.RequestToChannel(
                SampleNames.ApiChannel,
                new AuthenticatePlayerReq { AccessToken = request.AccessToken })
            .Async<AuthenticatePlayerRes>(cancellationToken);

        if (!authenticated.Accepted
            || string.IsNullOrWhiteSpace(authenticated.ActorId)
            || string.IsNullOrWhiteSpace(authenticated.DisplayName))
            throw new InvalidOperationException(authenticated.Reason ?? "Player authentication failed.");

        var ensured = await routes.Request(
                SampleNames.PlayChannel,
                session.PreferredPlayNodeRid,
                new EnsurePlayerActorReq
                {
                    ActorId = authenticated.ActorId,
                    DisplayName = authenticated.DisplayName,
                    PreferredActorNodeRid = session.PreferredPlayNodeRid.ToString()
                })
            .Async<EnsurePlayerActorRes>(cancellationToken);

        await context.Actors.BindAsync(
            ToActorRef(ensured.Actor),
            cancellationToken);

        await context.Client.Reply(new AuthenticateRes
            {
                ActorId = ensured.ActorId,
                DisplayName = authenticated.DisplayName,
                ActorNodeRid = ensured.Actor.NodeRid
            })
            .Async();
    }

    private static ActorRef ToActorRef(ActorRefSnapshot snapshot)
    {
        return new ActorRef(
            RoutingId.From(snapshot.NodeRid),
            snapshot.ActorId,
            snapshot.Generation);
    }
}