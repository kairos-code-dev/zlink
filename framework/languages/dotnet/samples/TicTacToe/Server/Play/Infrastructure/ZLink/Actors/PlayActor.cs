using TicTacToe.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;

namespace TicTacToe.Server.Play.Infrastructure.ZLink.Actors;

internal sealed class PlayActor(
    string actorId,
    IZLinkActorContext context)
    : IZLinkActor
{
    public string RoomId { get; private set; } = string.Empty;

    public PlayerInfo? Player { get; private set; }

    public bool DestroyAfterEntrySpotJoin { get; private set; }

    public bool Disconnected { get; private set; }
    public string ActorId { get; } = actorId;

    public IZLinkActorContext Context { get; } = context;

    public void ApplyPlayer(PlayerInfo player)
    {
        if (!string.Equals(player.ActorId, ActorId, StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"Authenticated player '{player.ActorId}' does not match actor '{ActorId}'.");

        Player = player;
    }

    public PlayerInfo RequirePlayer()
    {
        return Player ?? throw new InvalidOperationException("Actor has not been authenticated.");
    }

    public void JoinRoom(string roomId)
    {
        if (string.IsNullOrWhiteSpace(roomId))
            throw new ArgumentException("Room id must not be empty.", nameof(roomId));

        RoomId = roomId;
    }

    public string RequireJoinedRoom()
    {
        if (Context.SpotRid is null || string.IsNullOrEmpty(RoomId))
            throw new InvalidOperationException("Actor has not joined a room.");

        return RoomId;
    }

    public void MarkForDestroyAfterRoomLeave()
    {
        DestroyAfterEntrySpotJoin = true;
    }

    public void MarkDisconnected()
    {
        Disconnected = true;
    }
}
