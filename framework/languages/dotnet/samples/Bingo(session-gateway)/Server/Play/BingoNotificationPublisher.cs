using Bingo.SessionGateway.Shared.Configuration;
using Bingo.SessionGateway.Shared.Contracts;

namespace Bingo.SessionGateway.Play;

internal sealed class BingoNotificationPublisher(IZLinkActorSessionClient sessionClient)
{
    public async ValueTask PublishAsync(
        IReadOnlyList<BingoRoomEvent> events,
        CancellationToken cancellationToken)
    {
        foreach (var roomEvent in events)
        {
            await PublishAsync(roomEvent, cancellationToken).ConfigureAwait(false);
        }
    }

    private ValueTask PublishAsync(
        BingoRoomEvent roomEvent,
        CancellationToken cancellationToken)
    {
        return roomEvent.Kind switch
        {
            BingoRoomEventKind.PlayerJoined => sessionClient
                .Send(
                    roomEvent.RecipientActorId,
                    new PlayerJoinedNotify(
                        roomEvent.State.RoomId,
                        roomEvent.JoinedActorId ?? throw new InvalidOperationException("Joined actor is required."),
                        roomEvent.JoinedDisplayName ?? throw new InvalidOperationException("Joined display name is required."),
                        roomEvent.Seat,
                        roomEvent.IsHost,
                        roomEvent.State))
                .PacketName(SampleNames.PlayerJoinedPacket)
                .Submit(cancellationToken),
            BingoRoomEventKind.GameStarted => sessionClient
                .Send(roomEvent.RecipientActorId, new BingoGameStartedNotify(roomEvent.State))
                .PacketName(SampleNames.GameStartedPacket)
                .Submit(cancellationToken),
            BingoRoomEventKind.NumberDrawn => sessionClient
                .Send(
                    roomEvent.RecipientActorId,
                    new BingoNumberDrawnNotify(
                        roomEvent.State.RoomId,
                        roomEvent.State.DrawSeq,
                        roomEvent.DrawnNumber,
                        roomEvent.State))
                .PacketName(SampleNames.NumberDrawnPacket)
                .Submit(cancellationToken),
            BingoRoomEventKind.State => sessionClient
                .Send(roomEvent.RecipientActorId, new BingoStateNotify(roomEvent.State))
                .PacketName(SampleNames.StatePacket)
                .Submit(cancellationToken),
            BingoRoomEventKind.GameEnded => sessionClient
                .Send(roomEvent.RecipientActorId, new BingoGameEndedNotify(roomEvent.State))
                .PacketName(SampleNames.GameEndedPacket)
                .Submit(cancellationToken),
            _ => throw new InvalidOperationException($"Unsupported event kind {roomEvent.Kind}.")
        };
    }
}
