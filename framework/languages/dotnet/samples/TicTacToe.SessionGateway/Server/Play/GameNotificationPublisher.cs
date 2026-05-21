using TicTacToe.SessionGateway.Play;
using TicTacToe.SessionGateway.Shared.Configuration;
using TicTacToe.SessionGateway.Shared.Contracts;

namespace TicTacToe.SessionActorDispatch.Play;

internal sealed class GameNotificationPublisher
{
    public async ValueTask PublishAsync(
        IReadOnlyList<TicTacToeGameEvent> events,
        CancellationToken cancellationToken)
    {
        foreach (var gameEvent in events)
        {
            await PublishAsync(gameEvent, cancellationToken).ConfigureAwait(false);
        }
    }

    private static ValueTask PublishAsync(
        TicTacToeGameEvent gameEvent,
        CancellationToken cancellationToken)
    {
        return gameEvent switch
        {
            { Kind: TicTacToeGameEventKind.OpponentJoined } joined => joined.Recipient.Context.SessionProxy
                .Send(
                    new OpponentJoinedNotify(
                        joined.Snapshot.MatchId,
                        joined.JoinedActorId
                            ?? throw new InvalidOperationException("Opponent joined event must include the joined actor id."),
                        joined.JoinedMark?.ToContract()
                            ?? throw new InvalidOperationException("Opponent joined event must include the joined mark."),
                        joined.Snapshot.ToContract()))
                .PacketName(SampleNames.OpponentJoinedPacket)
                .Submit(cancellationToken),
            { Kind: TicTacToeGameEventKind.TurnChanged } turn => turn.Recipient.Context.SessionProxy
                .Send(
                    new TurnChangedNotify(
                        turn.Snapshot.MatchId,
                        turn.Snapshot.TurnActorId,
                        turn.Snapshot.ToContract()))
                .PacketName(SampleNames.TurnChangedPacket)
                .Submit(cancellationToken),
            { Kind: TicTacToeGameEventKind.GameEnded } ended => ended.Recipient.Context.SessionProxy
                .Send(
                    new GameEndedNotify(
                        ended.Snapshot.MatchId,
                        ended.Snapshot.WinnerActorId,
                        ended.Snapshot.Status == TicTacToeGameStatus.Draw,
                        ended.Snapshot.ToContract()))
                .PacketName(SampleNames.GameEndedPacket)
                .Submit(cancellationToken),
            _ => throw new InvalidOperationException($"Unsupported game event '{gameEvent.GetType().Name}'.")
        };
    }
}
