using System.Collections.Concurrent;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Json;
using TicTacToe.SessionGateway.Shared.Configuration;
using TicTacToe.SessionGateway.Shared.Contracts;

namespace TicTacToe.SessionGateway.Client;

internal sealed class SessionActorNotificationInbox
{
    private readonly ConcurrentQueue<TurnChangedNotify> _turnChanged = new();
    private readonly ConcurrentQueue<OpponentJoinedNotify> _opponentJoined = new();
    private readonly ConcurrentQueue<GameEndedNotify> _gameEnded = new();
    private readonly object _notificationGate = new();
    private TaskCompletionSource _notificationArrived =
        new(TaskCreationOptions.RunContinuationsAsynchronously);

    public IReadOnlyList<TurnChangedNotify> TurnChanged => _turnChanged.ToArray();

    public IReadOnlyList<OpponentJoinedNotify> OpponentJoined => _opponentJoined.ToArray();

    public IReadOnlyList<GameEndedNotify> GameEnded => _gameEnded.ToArray();

    public void Register(IZlinkStreamConnector connector)
    {
        _ = connector.On<TurnChangedNotify>(
            SampleNames.TurnChangedPacket,
            (message, _) =>
            {
                _turnChanged.Enqueue(message.Payload);
                Signal();
                return ValueTask.CompletedTask;
            });
        _ = connector.On<OpponentJoinedNotify>(
            SampleNames.OpponentJoinedPacket,
            (message, _) =>
            {
                _opponentJoined.Enqueue(message.Payload);
                Signal();
                return ValueTask.CompletedTask;
            });
        _ = connector.On<GameEndedNotify>(
            SampleNames.GameEndedPacket,
            (message, _) =>
            {
                _gameEnded.Enqueue(message.Payload);
                Signal();
                return ValueTask.CompletedTask;
            });
    }

    public Task WaitAsync(CancellationToken cancellationToken)
    {
        lock (_notificationGate)
        {
            return _notificationArrived.Task.WaitAsync(cancellationToken);
        }
    }

    private void Signal()
    {
        TaskCompletionSource arrived;
        lock (_notificationGate)
        {
            arrived = _notificationArrived;
            _notificationArrived = new TaskCompletionSource(
                TaskCreationOptions.RunContinuationsAsynchronously);
        }

        arrived.TrySetResult();
    }
}
