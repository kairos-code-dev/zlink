using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Timers;
using TicTacToe.SessionGateway.Server.Play;
using TicTacToe.SessionGateway.Shared.Configuration;
using TicTacToe.SessionGateway.Shared.Contracts;

namespace TicTacToe.SessionGateway.Server.Play.GameSpots;

internal sealed class GameNotificationPublisher
{
    public async ValueTask PublishAsync(
        IReadOnlyList<TicTacToeGameEvent> events,
        CancellationToken cancellationToken)
    {
        foreach (var gameEvent in events)
        {
            await PublishAsync(gameEvent, cancellationToken);
        }
    }

    private static ValueTask PublishAsync(
        TicTacToeGameEvent gameEvent,
        CancellationToken cancellationToken)
    {
        return gameEvent switch
        {
            { Kind: TicTacToeGameEventKind.OpponentJoined } joined => joined.Recipient.Context.BoundSession
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
            { Kind: TicTacToeGameEventKind.TurnChanged } turn => turn.Recipient.Context.BoundSession
                .Send(
                    new TurnChangedNotify(
                        turn.Snapshot.MatchId,
                        turn.Snapshot.TurnActorId,
                        turn.Snapshot.ToContract()))
                .PacketName(SampleNames.TurnChangedPacket)
                .Submit(cancellationToken),
            { Kind: TicTacToeGameEventKind.GameEnded } ended => ended.Recipient.Context.BoundSession
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
