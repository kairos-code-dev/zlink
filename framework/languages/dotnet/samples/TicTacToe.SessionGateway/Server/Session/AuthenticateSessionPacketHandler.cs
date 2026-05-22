using Systems.Zlink;
using Systems.Zlink.Stream.Connector.Contracts;
using TicTacToe.SessionGateway.Shared.Configuration;
using TicTacToe.SessionGateway.Shared.Contracts;

namespace TicTacToe.SessionActorDispatch.Session;

internal sealed class AuthenticateSessionPacketHandler : ISessionRelayPacketHandler
{
    public string PacketName => nameof(AuthenticateReq);

    public async ValueTask HandleAsync(
        SessionRelayPacketContext context,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        _ = header;
        using (payload)
        {
            var request = SessionRelayJson.Decode<AuthenticateReq>(payload);
            var authenticated = await context.Stream.RequestChannel(
                    SampleNames.ApiChannel,
                    new AuthenticateActorReq(request.ActorId))
                .Timeout(SampleTimings.RequestTimeout)
                .SubmitAsync<AuthenticateActorRes>(cancellationToken)
                .ConfigureAwait(false);
            if (!authenticated.Accepted || string.IsNullOrWhiteSpace(authenticated.ActorId))
            {
                throw new InvalidOperationException(authenticated.Reason ?? "Actor authentication failed.");
            }

            var ensured = await context.Stream.RequestChannel(
                    SampleNames.PlayChannel,
                    new EnsurePlayerActorReq(authenticated.ActorId))
                .Timeout(SampleTimings.RequestTimeout)
                .SubmitAsync<EnsurePlayerActorRes>(cancellationToken)
                .ConfigureAwait(false);

            context.State.AttachAuthenticatedActor(ensured.ActorId, ensured.Route);
            await context.State.RequireActorAsync(
                    context.Stream,
                    "binding authenticated player actor",
                    cancellationToken)
                .ConfigureAwait(false);

            await context.Stream.Reply(new AuthenticateRes(ensured.ActorId))
                .Submit(cancellationToken)
                .ConfigureAwait(false);
        }
    }
}
