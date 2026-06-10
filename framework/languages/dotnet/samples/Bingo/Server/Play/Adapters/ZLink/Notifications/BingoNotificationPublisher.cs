using Bingo.Server.Play.Domain.Bingo;
using Bingo.Shared.Configuration;
using Bingo.Shared.Contracts;

namespace Bingo.Server.Play.Adapters.ZLink.Notifications;

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
                    new PlayerJoinedNotify
                    {
                        RoomId = roomEvent.State.RoomId,
                        ActorId = roomEvent.JoinedActorId ?? throw new InvalidOperationException("Joined actor is required."),
                        DisplayName = roomEvent.JoinedDisplayName ?? throw new InvalidOperationException("Joined display name is required."),
                        Seat = roomEvent.Seat,
                        IsHost = roomEvent.IsHost,
                        State = roomEvent.State,
                    })
                .PacketName(SampleNames.PlayerJoinedPacket)
                .SubmitAsync(cancellationToken),
            BingoRoomEventKind.GameStarted => roomEvent.Recipient.Context.BoundSession
                .Send(new BingoGameStartedNotify { State = roomEvent.State })
                .PacketName(SampleNames.GameStartedPacket)
                .SubmitAsync(cancellationToken),
            BingoRoomEventKind.NumberDrawn => roomEvent.Recipient.Context.BoundSession
                .Send(
                    new BingoNumberDrawnNotify
                    {
                        RoomId = roomEvent.State.RoomId,
                        DrawSeq = roomEvent.State.DrawSeq,
                        Number = roomEvent.DrawnNumber,
                        State = roomEvent.State,
                    })
                .PacketName(SampleNames.NumberDrawnPacket)
                .SubmitAsync(cancellationToken),
            BingoRoomEventKind.GameEnded => roomEvent.Recipient.Context.BoundSession
                .Send(new BingoGameEndedNotify { State = roomEvent.State })
                .PacketName(SampleNames.GameEndedPacket)
                .SubmitAsync(cancellationToken),
            _ => throw new InvalidOperationException($"Unsupported event kind {roomEvent.Kind}.")
        };
    }
}
