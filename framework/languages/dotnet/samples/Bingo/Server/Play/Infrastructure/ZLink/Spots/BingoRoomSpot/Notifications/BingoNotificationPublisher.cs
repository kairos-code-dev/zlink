using Bingo.Server.Play.Domain.Bingo;
using Bingo.Shared.Contracts;

namespace Bingo.Server.Play.Infrastructure.ZLink.Spots.BingoRoomSpot.Notifications;

internal sealed class BingoNotificationPublisher
{
    public ValueTask PublishAsync(
        IReadOnlyList<BingoRoomEvent> events,
        CancellationToken cancellationToken)
    {
        foreach (var roomEvent in events) Publish(roomEvent, cancellationToken);
        return ValueTask.CompletedTask;
    }

    private static void Publish(
        BingoRoomEvent roomEvent,
        CancellationToken cancellationToken)
    {
        switch (roomEvent.Kind)
        {
            case BingoRoomEventKind.PlayerJoined:
                roomEvent.Recipient.Context.BoundSession
                .Send(
                    new PlayerJoinedNotify
                    {
                        RoomId = roomEvent.State.RoomId,
                        ActorId = roomEvent.JoinedActorId ??
                                  throw new InvalidOperationException("Joined actor is required."),
                        DisplayName = roomEvent.JoinedDisplayName ??
                                      throw new InvalidOperationException("Joined display name is required."),
                        Seat = roomEvent.Seat,
                        IsHost = roomEvent.IsHost,
                        State = roomEvent.State
                    })
                .Submit(cancellationToken);
                break;
            case BingoRoomEventKind.GameStarted:
                roomEvent.Recipient.Context.BoundSession
                .Send(new BingoGameStartedNotify { State = roomEvent.State })
                .Submit(cancellationToken);
                break;
            case BingoRoomEventKind.NumberDrawn:
                roomEvent.Recipient.Context.BoundSession
                .Send(
                    new BingoNumberDrawnNotify
                    {
                        RoomId = roomEvent.State.RoomId,
                        DrawSeq = roomEvent.State.DrawSeq,
                        Number = roomEvent.DrawnNumber,
                        State = roomEvent.State
                    })
                .Submit(cancellationToken);
                break;
            case BingoRoomEventKind.GameEnded:
                roomEvent.Recipient.Context.BoundSession
                .Send(new BingoGameEndedNotify { State = roomEvent.State })
                .Submit(cancellationToken);
                break;
            default:
                throw new InvalidOperationException($"Unsupported event kind {roomEvent.Kind}.");
        }
    }
}
