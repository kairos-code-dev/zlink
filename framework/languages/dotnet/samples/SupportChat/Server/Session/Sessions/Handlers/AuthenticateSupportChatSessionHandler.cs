using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using SupportChat.Shared.Configuration;
using SupportChat.Shared.Contracts;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Streams;
using Systems.Zlink.Stream.Connector.Contracts;

namespace SupportChat.Server.Session.Sessions.Handlers;

internal sealed class AuthenticateSupportChatSessionHandler(IZLinkChannelClient channels)
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
                new AuthenticateUserReq(request.AccessToken))
            .SubmitAsync<AuthenticateUserRes>(cancellationToken);

        if (!authenticated.Accepted
            || string.IsNullOrWhiteSpace(authenticated.ActorId)
            || string.IsNullOrWhiteSpace(authenticated.DisplayName)
            || string.IsNullOrWhiteSpace(authenticated.Role))
        {
            throw new InvalidOperationException(authenticated.Reason ?? "SupportChat authentication failed.");
        }

        var ensured = await channels.RequestToChannel(
                SampleNames.SupportChannel,
                new EnsureSupportUserActorReq(
                    authenticated.ActorId,
                    authenticated.DisplayName,
                    authenticated.Role))
            .SubmitAsync<EnsureSupportUserActorRes>(cancellationToken);

        await context.Actors.BindAsync(
            ToActorRef(ensured.Actor),
            cancellationToken);

        await context.Client.Reply(new AuthenticateRes(
                authenticated.ActorId,
                authenticated.DisplayName,
                authenticated.Role))
            .SubmitAsync();
    }

    private static ActorRef ToActorRef(ActorRefSnapshot snapshot)
    {
        return new ActorRef(
            RoutingId.From(snapshot.NodeRid),
            snapshot.ActorId,
            snapshot.Generation);
    }
}
