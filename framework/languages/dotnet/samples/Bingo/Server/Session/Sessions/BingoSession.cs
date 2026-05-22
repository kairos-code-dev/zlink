using Systems.Zlink.Stream.Connector.Contracts;

namespace Bingo.Server.Session.Sessions;

internal sealed class BingoSession(
    IZLinkSessionContext context,
    IEnumerable<IBingoSessionHandler> handlers) : IZLinkSession
{
    private readonly IReadOnlyDictionary<string, IBingoSessionHandler> _handlers =
        handlers.ToDictionary(static handler => handler.PacketName, StringComparer.Ordinal);
    private readonly SessionRelayState _state = new();

    public IZLinkSessionContext Context { get; } = context;

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
                new BingoSessionContext(Context, _state),
                header,
                payload.Move(),
                cancellationToken)
            ;
    }
}
