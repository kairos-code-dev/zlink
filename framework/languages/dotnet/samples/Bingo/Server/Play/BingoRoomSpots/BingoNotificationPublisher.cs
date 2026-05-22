using Bingo.Shared.Configuration;
using Bingo.Shared.Contracts;

namespace Bingo.Server.Play.BingoRoomSpots;

internal sealed class BingoNotificationPublisher
{
    public async ValueTask PublishAsync(
        IReadOnlyList<BingoRoomEvent> events,
        CancellationToken cancellationToken)
    {
        foreach (var roomEvent in events)
        {
            await PublishAsync(roomEvent, cancellationToken);
        }
    }

    private ValueTask PublishAsync(
        BingoRoomEvent roomEvent,
        CancellationToken cancellationToken)
    {
        return roomEvent.Kind switch
        {
            BingoRoomEventKind.PlayerJoined => roomEvent.Recipient.Context.BoundSession
                .Send(
                    new PlayerJoinedNotify(
                        roomEvent.State.RoomId,
                        roomEvent.JoinedActorId ?? throw new InvalidOperationException("Joined actor is required."),
                        roomEvent.JoinedDisplayName ?? throw new InvalidOperationException("Joined display name is required."),
                        roomEvent.Seat,
                        roomEvent.IsHost,
                        roomEvent.State))
                .PacketName(SampleNames.PlayerJoinedPacket)
                .Submit(cancellationToken),
            BingoRoomEventKind.GameStarted => roomEvent.Recipient.Context.BoundSession
                .Send(new BingoGameStartedNotify(roomEvent.State))
                .PacketName(SampleNames.GameStartedPacket)
                .Submit(cancellationToken),
            BingoRoomEventKind.NumberDrawn => roomEvent.Recipient.Context.BoundSession
                .Send(
                    new BingoNumberDrawnNotify(
                        roomEvent.State.RoomId,
                        roomEvent.State.DrawSeq,
                        roomEvent.DrawnNumber,
                        roomEvent.State))
                .PacketName(SampleNames.NumberDrawnPacket)
                .Submit(cancellationToken),
            BingoRoomEventKind.State => roomEvent.Recipient.Context.BoundSession
                .Send(new BingoStateNotify(roomEvent.State))
                .PacketName(SampleNames.StatePacket)
                .Submit(cancellationToken),
            BingoRoomEventKind.GameEnded => roomEvent.Recipient.Context.BoundSession
                .Send(new BingoGameEndedNotify(roomEvent.State))
                .PacketName(SampleNames.GameEndedPacket)
                .Submit(cancellationToken),
            _ => throw new InvalidOperationException($"Unsupported event kind {roomEvent.Kind}.")
        };
    }
}
