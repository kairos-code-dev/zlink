using Systems.Zlink.Stream.Connector.Contracts;
using TicTacToe.SessionGateway.Configuration;
using TicTacToe.SessionGateway.Contracts;
using Zlink;
using Zlink.Codecs.Json;
using Zlink.Framework.Streams;

namespace TicTacToe.SessionGateway.Session;

internal sealed class SessionRelaySession(SessionRelaySettings settings) : IZLinkSession
{
    private string? _actorId;

    public IZLinkSessionContext Context { get; set; } = default!;

    public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
    {
        return Context.UnbindActorAsync(cancellationToken);
    }

    public ValueTask OnErrorAsync(
        ZLinkStreamError error,
        CancellationToken cancellationToken)
    {
        _ = error;
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }

    public async ValueTask OnDispatchAsync(
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        if (header.Name == nameof(AuthenticateReq))
        {
            await AuthenticateAsync(payload, cancellationToken).ConfigureAwait(false);
            return;
        }

        var actorId = _actorId
            ?? throw new InvalidOperationException("Client must authenticate before sending game packets.");

        switch (header.Name)
        {
            case nameof(CreateGameReq):
                await CreateGameAsync(actorId, payload, cancellationToken).ConfigureAwait(false);
                break;
            case nameof(JoinGameReq):
                await RelayGamePacketAsync(
                    actorId,
                    payload.FromJson<JoinGameReq>().GameId,
                    header,
                    payload,
                    cancellationToken).ConfigureAwait(false);
                break;
            case nameof(PlaceMarkReq):
                await RelayGamePacketAsync(
                    actorId,
                    payload.FromJson<PlaceMarkReq>().GameId,
                    header,
                    payload,
                    cancellationToken).ConfigureAwait(false);
                break;
            default:
                throw new InvalidOperationException($"Unsupported session packet '{header.Name}'.");
        }
    }

    private async ValueTask AuthenticateAsync(
        Message payload,
        CancellationToken cancellationToken)
    {
        using (payload)
        {
            var request = payload.FromJson<AuthenticateReq>();
            _actorId = request.PlayerId;
            await Context.BindActorAsync(request.PlayerId, cancellationToken).ConfigureAwait(false);
            await Context.Reply(new AuthenticateRes(request.PlayerId))
                .Async(cancellationToken)
                .ConfigureAwait(false);
        }
    }

    private async ValueTask CreateGameAsync(
        string actorId,
        Message payload,
        CancellationToken cancellationToken)
    {
        var request = payload.FromJson<CreateGameReq>();
        using var reply = await RequestApiAsync(actorId, request, nameof(CreateGameReq), cancellationToken)
            .ConfigureAwait(false);
        var response = reply.FromJson<CreateGameRes>();
        await Context.Reply(response)
            .Async(cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask RelayGamePacketAsync(
        string actorId,
        string gameId,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        var playRid = await ResolvePlayRidAsync(actorId, gameId, cancellationToken)
            .ConfigureAwait(false);
        await Context.OpenActorRelay(
                SampleNames.RouterChannel,
                playRid,
                actorId)
            .DispatchAsync(header, payload, cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<RoutingId> ResolvePlayRidAsync(
        string actorId,
        string gameId,
        CancellationToken cancellationToken)
    {
        using var reply = await RequestApiAsync(
                actorId,
                new ResolveGameReq(gameId),
                nameof(ResolveGameReq),
                cancellationToken)
            .ConfigureAwait(false);
        var response = reply.FromJson<ResolveGameRes>();
        return RoutingId.FromString(response.PlayNodeRid);
    }

    private ValueTask<Message> RequestApiAsync<TRequest>(
        string actorId,
        TRequest request,
        string packetName,
        CancellationToken cancellationToken)
    {
        return Context.OpenActorRelay(
                SampleNames.RouterChannel,
                settings.ApiRid,
                actorId)
            .Request(request)
            .WithPacketName(packetName)
            .WithTimeout(SampleTimings.RequestTimeout)
            .Async(cancellationToken);
    }
}
