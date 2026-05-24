using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using TicTacToe.SessionGateway.Shared.Configuration;
using TicTacToe.SessionGateway.Shared.Contracts;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Streams;

namespace TicTacToe.SessionGateway.Server.Session.Sessions.Handlers;

internal sealed class CreateMatchSessionPacketHandler(IZLinkClient channels)
    : IZLinkSessionPacketHandler<IZLinkSessionContext>
{
    public string PacketName => nameof(CreateMatchReq);

    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        _ = header;
        var actorId = RequireSingleBoundActor(context, "creating a match").ActorId;
        var request = payload.Decode<CreateMatchReq>();
        var reply = await channels.Request(
                SampleNames.ApiChannel,
                request with { OwnerActorId = actorId })
            .Timeout(SampleTimings.RequestTimeout)
            .SubmitAsync<CreateMatchRes>(cancellationToken)
            ;

        await context.Reply(reply)
            .Submit(cancellationToken)
            ;
    }

    private static IZLinkActorRef RequireSingleBoundActor(
        IZLinkSessionContext context,
        string action)
    {
        var actors = context.BoundActors;
        return actors.Count switch
        {
            1 => actors.Single(),
            0 => throw new InvalidOperationException($"Client must authenticate before {action}."),
            _ => throw new InvalidOperationException($"Exactly one actor must be bound before {action}.")
        };
    }
}
