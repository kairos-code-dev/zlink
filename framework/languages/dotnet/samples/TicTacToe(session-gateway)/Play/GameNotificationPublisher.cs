using TicTacToe.SessionActorDispatch.Configuration;
using TicTacToe.SessionActorDispatch.Contracts;
using TicTacToe.SessionGateway.Play;
using Zlink.Framework.Streams;

namespace TicTacToe.SessionActorDispatch.Play;

internal sealed class GameNotificationPublisher(IZLinkSessionProxy sessionProxy)
{
    public async ValueTask PublishAsync(
        IReadOnlyList<TicTacToeGameEvent> events,
        CancellationToken cancellationToken)
    {
        foreach (var gameEvent in events)
        {
            await PublishAsync(sessionProxy, gameEvent, cancellationToken).ConfigureAwait(false);
        }
    }

    private static ValueTask PublishAsync(
        IZLinkSessionProxy proxy,
        TicTacToeGameEvent gameEvent,
        CancellationToken cancellationToken)
    {
        return gameEvent switch
        {
            OpponentJoinedGameEvent joined => proxy
                .Send(
                    joined.RecipientActorId,
                    new OpponentJoinedNotify(
                        joined.Snapshot.MatchId,
                        joined.JoinedActorId,
                        joined.Mark.ToContract(),
                        joined.Snapshot.ToContract()))
                .WithPacketName(SampleNames.OpponentJoinedPacket)
                .Async(cancellationToken),
            TurnChangedGameEvent turn => proxy
                .Send(
                    turn.RecipientActorId,
                    new TurnChangedNotify(
                        turn.Snapshot.MatchId,
                        turn.Snapshot.TurnActorId,
                        turn.Snapshot.ToContract()))
                .WithPacketName(SampleNames.TurnChangedPacket)
                .Async(cancellationToken),
            GameEndedGameEvent ended => proxy
                .Send(
                    ended.RecipientActorId,
                    new GameEndedNotify(
                        ended.Snapshot.MatchId,
                        ended.Snapshot.WinnerActorId,
                        ended.Snapshot.Status == TicTacToeGameStatus.Draw,
                        ended.Snapshot.ToContract()))
                .WithPacketName(SampleNames.GameEndedPacket)
                .Async(cancellationToken),
            _ => throw new InvalidOperationException($"Unsupported game event '{gameEvent.GetType().Name}'.")
        };
    }
}
