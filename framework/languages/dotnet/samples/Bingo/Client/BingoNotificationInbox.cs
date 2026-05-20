using System.Collections.Concurrent;
using Bingo.Shared.Configuration;
using Bingo.Shared.Contracts;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Json;

namespace Bingo.Client;

internal sealed class BingoNotificationInbox
{
    private readonly ConcurrentQueue<PlayerJoinedNotify> _playerJoined = new();
    private readonly ConcurrentQueue<BingoGameStartedNotify> _started = new();
    private readonly ConcurrentQueue<BingoNumberDrawnNotify> _drawn = new();
    private readonly ConcurrentQueue<BingoStateNotify> _state = new();
    private readonly ConcurrentQueue<BingoGameEndedNotify> _ended = new();
    private readonly object _gate = new();
    private TaskCompletionSource _arrived = new(TaskCreationOptions.RunContinuationsAsynchronously);

    public IReadOnlyList<PlayerJoinedNotify> PlayerJoined => _playerJoined.ToArray();

    public IReadOnlyList<BingoGameStartedNotify> Started => _started.ToArray();

    public IReadOnlyList<BingoNumberDrawnNotify> Drawn => _drawn.ToArray();

    public IReadOnlyList<BingoStateNotify> State => _state.ToArray();

    public IReadOnlyList<BingoGameEndedNotify> Ended => _ended.ToArray();

    public void Register(IZlinkStreamConnector connector)
    {
        _ = connector.On<PlayerJoinedNotify>(SampleNames.PlayerJoinedPacket, (message, _) =>
        {
            _playerJoined.Enqueue(message.Payload);
            Signal();
            return ValueTask.CompletedTask;
        });
        _ = connector.On<BingoGameStartedNotify>(SampleNames.GameStartedPacket, (message, _) =>
        {
            _started.Enqueue(message.Payload);
            Signal();
            return ValueTask.CompletedTask;
        });
        _ = connector.On<BingoNumberDrawnNotify>(SampleNames.NumberDrawnPacket, (message, _) =>
        {
            _drawn.Enqueue(message.Payload);
            Signal();
            return ValueTask.CompletedTask;
        });
        _ = connector.On<BingoStateNotify>(SampleNames.StatePacket, (message, _) =>
        {
            _state.Enqueue(message.Payload);
            Signal();
            return ValueTask.CompletedTask;
        });
        _ = connector.On<BingoGameEndedNotify>(SampleNames.GameEndedPacket, (message, _) =>
        {
            _ended.Enqueue(message.Payload);
            Signal();
            return ValueTask.CompletedTask;
        });
    }

    public Task WaitAsync(CancellationToken cancellationToken)
    {
        lock (_gate)
        {
            return _arrived.Task.WaitAsync(cancellationToken);
        }
    }

    private void Signal()
    {
        TaskCompletionSource arrived;
        lock (_gate)
        {
            arrived = _arrived;
            _arrived = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        }

        arrived.TrySetResult();
    }
}
