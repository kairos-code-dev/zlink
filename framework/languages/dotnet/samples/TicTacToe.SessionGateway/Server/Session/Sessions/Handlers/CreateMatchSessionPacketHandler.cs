using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using Systems.Zlink.Stream.Connector.Contracts;
using TicTacToe.SessionGateway.Shared.Configuration;
using TicTacToe.SessionGateway.Shared.Contracts;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Streams;

namespace TicTacToe.SessionGateway.Session.Sessions.Handlers
{
    internal sealed class CreateMatchSessionPacketHandler(IZLinkChannelClient channels)
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
            var reply = await channels.RequestToChannel(
                        SampleNames.ApiChannel,
                        new CreateMatchReq(OwnerActorId: actorId))
                    .Timeout(SampleTimings.RequestTimeout)
                    .SubmitAsync<CreateMatchRes>(cancellationToken) ;

            await context.Client.Reply(reply)
                .Submit();
        }

        private static IZLinkSessionActor RequireSingleBoundActor(
            IZLinkSessionContext context,
            string action)
        {
            var actors = context.Actors.Bound;
            return actors.Count switch
            {
                1 => actors.Single(),
                0 => throw new InvalidOperationException($"Client must authenticate before {action}."),
                _ => throw new InvalidOperationException($"Exactly one actor must be bound before {action}.")
            };
        }
    }
}
