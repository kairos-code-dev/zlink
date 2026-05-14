using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using TicTacToe.SessionGateway.Shared.Configuration;
using TicTacToe.SessionGateway.Shared.Contracts;
using Zlink.Framework.Contracts.Streams;

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
            var request = SessionRelayJson.Decode<CreateMatchReq>(payload);
            var reply = await context.Stream.RequestChannel(
                    SampleNames.ApiChannel,
                    request with { OwnerActorId = actorId })
                .Timeout(SampleTimings.RequestTimeout)
                .SubmitAsync<CreateMatchRes>(cancellationToken)
                .ConfigureAwait(false);

            await context.Stream.Reply(reply)
                .Submit(cancellationToken)
                .ConfigureAwait(false);
        }
    }
}
