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

    public string ZoneId { get; private set; } = string.Empty;

    public bool IsBot { get; private set; }

    /// <summary>Patrol direction. Meaningful only for a bot; it reverses when a move is
    /// rejected so the bot walks back the way it came (§2.7).</summary>
    public int DirX { get; private set; }

    public int DirY { get; private set; }

    public bool HasEnteredWorld => ZoneId.Length > 0;

    public void Restore(int x, int y, string zoneId, bool isBot, int dirX, int dirY)
    {
        Position = new PlayerPosition(x, y);
        ZoneId = zoneId;
        IsBot = isBot;
        DirX = dirX;
        DirY = dirY;
    }

    public void SetPatrol(int dirX, int dirY)
    {
        DirX = dirX;
        DirY = dirY;
    }

    public void MoveTo(PlayerPosition position)
    {
        Position = position;
        ZoneId = position.ZoneId;
    }

    public void ReverseDirection()
    {
        DirX = -DirX;
        DirY = -DirY;
    }
}
