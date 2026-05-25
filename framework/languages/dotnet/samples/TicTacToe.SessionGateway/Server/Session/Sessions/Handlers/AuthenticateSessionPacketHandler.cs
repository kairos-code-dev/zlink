using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Timers;
using TicTacToe.SessionGateway.Shared.Actors;
using TicTacToe.SessionGateway.Shared.Configuration;
using TicTacToe.SessionGateway.Shared.Contracts;

namespace TicTacToe.SessionGateway.Server.Session.Sessions.Handlers;

internal sealed class AuthenticateSessionPacketHandler(
    IZLinkClient channels,
    IZLinkActorManager actors,
    SampleTopology topology)
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

        var actor = (PlayerActor)await actors.GetOrCreateAsync(
                authenticated.ActorId,
                SampleNames.PlayerActorType,
                cancellationToken)
            ;

        var joined = await actor.Context.JoinEntrySpot(topology.PlayRid)
            .Timeout(SampleTimings.RequestTimeout)
            .SubmitAsync(cancellationToken)
            ;

        await context.BindActorHandleAsync(
                joined.ActorId,
                joined.ActorType,
                joined.RemoteAddress,
                cancellationToken)
            ;

        await context.Reply(new AuthenticateRes(joined.ActorId))
            .Submit(cancellationToken)
            ;
    }
}
