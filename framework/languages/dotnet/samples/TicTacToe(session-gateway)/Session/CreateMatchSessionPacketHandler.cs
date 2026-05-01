using Systems.Zlink.Stream.Connector.Contracts;
using TicTacToe.SessionActorDispatch.Configuration;
using TicTacToe.SessionActorDispatch.Contracts;
using Zlink;
using Zlink.Codecs.Json;
using Zlink.Framework.Streams;

namespace TicTacToe.SessionActorDispatch.Session;

internal sealed class CreateMatchSessionPacketHandler : ISessionRelayPacketHandler
{
    public string PacketName => nameof(CreateMatchReq);

    public async ValueTask HandleAsync(
        SessionRelayPacketContext context,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        _ = header;
        var actorId = context.State.RequireActorId("creating a match");
        using (payload)
        {
            var request = payload.FromJson<CreateMatchReq>();
            var reply = await context.Stream.RequestChannel(
                    SampleNames.ApiChannel,
                    request with { OwnerActorId = actorId })
                .WithTimeout(SampleTimings.RequestTimeout)
                .Async<CreateMatchRes>(cancellationToken)
                .ConfigureAwait(false);

            await context.Stream.Reply(reply)
                .Async(cancellationToken)
                .ConfigureAwait(false);
        }
    }
}
