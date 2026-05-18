using Systems.Zlink;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.Contracts.Streams;

namespace TicTacToe.SessionActorDispatch.Session;

internal sealed class SessionRelaySession(
    IEnumerable<ISessionRelayPacketHandler> handlers) : IZLinkSession
{
    private readonly IReadOnlyDictionary<string, ISessionRelayPacketHandler> _handlers =
        handlers.ToDictionary(static handler => handler.PacketName, StringComparer.Ordinal);
    private readonly SessionRelayState _state = new();

    public IZLinkSessionContext Context { get; set; } = default!;

    public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        _state.Clear();
        return ValueTask.CompletedTask;
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
        if (!_handlers.TryGetValue(header.Name, out var handler))
        {
            throw new InvalidOperationException($"Unsupported client packet '{header.Name}'.");
        }

        await handler.HandleAsync(
                new SessionRelayPacketContext(Context, _state),
                header,
                payload.Move(),
                cancellationToken)
            .ConfigureAwait(false);
    }
}
