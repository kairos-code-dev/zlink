using Bingo.Shared.Configuration;
using Bingo.Shared.Contracts;
using Systems.Zlink.Stream.Connector.Contracts;

namespace Bingo.Server.Session.Sessions.Handlers;

internal sealed class AuthenticateBingoSessionHandler : IBingoSessionHandler
{
    public string PacketName => nameof(AuthenticateReq);

    public async ValueTask HandleAsync(
        BingoSessionContext context,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        _ = header;
        await using (payload)
        {
            var request = SessionRelayJson.Decode<AuthenticateReq>(payload);
            var authenticated = await context.Stream.RequestChannel(
                    SampleNames.ApiChannel,
                    new AuthenticatePlayerReq(request.AccessToken))
                .Timeout(SampleTimings.RequestTimeout)
                .SubmitAsync<AuthenticatePlayerRes>(cancellationToken)
                .ConfigureAwait(false);
            if (!authenticated.Accepted
                || string.IsNullOrWhiteSpace(authenticated.ActorId)
                || string.IsNullOrWhiteSpace(authenticated.DisplayName))
            {
                throw new InvalidOperationException(authenticated.Reason ?? "Player authentication failed.");
            }

            var ensured = await context.Stream.RequestChannel(
                    SampleNames.PlayChannel,
                    new EnsurePlayerActorReq(authenticated.ActorId, authenticated.DisplayName))
                .Timeout(SampleTimings.RequestTimeout)
                .SubmitAsync<EnsurePlayerActorRes>(cancellationToken)
                .ConfigureAwait(false);

            context.State.AttachAuthenticatedActor(
                ensured.ActorId,
                authenticated.DisplayName,
                ensured.Route);

            await context.State.RequireActorAsync(
                    context.Stream,
                    "binding authenticated player actor",
                    cancellationToken)
                .ConfigureAwait(false);

            await context.Stream.Reply(new AuthenticateRes(ensured.ActorId, authenticated.DisplayName))
                .Submit(cancellationToken)
                .ConfigureAwait(false);
        }
    }
}
