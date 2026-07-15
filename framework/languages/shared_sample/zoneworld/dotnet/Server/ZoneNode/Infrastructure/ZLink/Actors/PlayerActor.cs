using Zlink.Framework.Contracts.Actors;
using ZoneWorld.Server.ZoneNode.Domain.ZoneWorld;

namespace ZoneWorld.Server.ZoneNode.Infrastructure.ZLink.Actors;

/// <summary>
/// The authority for one player's coordinate, zone and node (§2.1). The zone spot keeps
/// a copy for rendering, but this is the value that decides a move.
///
/// A bot is the same type. The only difference is that it has no bound session, so no
/// client push is ever addressed to it (§2.7).
/// </summary>
public sealed class PlayerActor(string actorId, IZLinkActorContext context) : IZLinkActor
{
    public string ActorId { get; } = actorId;

    public IZLinkActorContext Context { get; } = context;

    public PlayerPosition Position { get; private set; }

    /// <summary>The zone the coordinate falls in. Derived, not stored: the authority for a
    /// player cannot be allowed to disagree with itself about where that player is (§2.1).</summary>
    public string ZoneId => Position.ZoneId;

    public bool IsBot { get; private set; }

    /// <summary>Patrol direction. Meaningful only for a bot; it reverses when a move is
    /// rejected so the bot walks back the way it came (§2.7).</summary>
    public int DirX { get; private set; }

    public int DirY { get; private set; }

    /// <summary>
    /// Rebuilds the player on the node it transferred to (§2.6). The zone travels on the wire
    /// alongside the coordinate, so it is checked rather than trusted: if the two disagree, the
    /// transfer is corrupt, and a player silently teleporting is worse than a failed transfer.
    /// </summary>
    public void Restore(int x, int y, string zoneId, bool isBot, int dirX, int dirY)
    {
        var position = new PlayerPosition(x, y);
        if (position.ZoneId != zoneId)
            throw new InvalidOperationException(
                $"Transferred state for '{ActorId}' disagrees with itself: ({x},{y}) is in "
                + $"'{position.ZoneId}', but the state says '{zoneId}'.");

        Position = position;
        IsBot = isBot;
        DirX = dirX;
        DirY = dirY;
    }

    public void SetPatrol(int dirX, int dirY)
    {
        DirX = dirX;
        DirY = dirY;
    }

    public void MoveTo(PlayerPosition position) => Position = position;

    public void ReverseDirection()
    {
        DirX = -DirX;
        DirY = -DirY;
    }
}
