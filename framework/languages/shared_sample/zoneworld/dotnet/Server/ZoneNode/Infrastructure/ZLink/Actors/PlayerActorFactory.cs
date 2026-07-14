using Systems.Zlink;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;

namespace ZoneWorld.Server.ZoneNode.Infrastructure.ZLink.Actors;

internal sealed class PlayerActorFactory : IZLinkActorFactory
{
    public ValueTask<IZLinkActor> CreateAsync(
        string actorId,
        IZLinkActorContext context,
        CancellationToken cancellationToken) =>
        ValueTask.FromResult<IZLinkActor>(new PlayerActor(actorId, context));
}

/// <summary>
/// Carries the player across a node boundary (§2.6). Without this adapter the framework
/// would fall back to an empty-state transfer and rebuild the actor from the factory —
/// the player would arrive in the next zone with its coordinate reset, and the sample
/// would not hold together, because the actor is the coordinate's authority (§2.1).
/// </summary>
internal sealed class PlayerActorTransferAdapter : IZLinkActorTransferAdapter<PlayerActor>
{
    public ValueTask<ZLinkMessage> TransferOutAsync(
        PlayerActor actor,
        CancellationToken cancellationToken) =>
        ValueTask.FromResult(ZLinkMessage.From(new PlayerTransferState(
            actor.Position.X,
            actor.Position.Y,
            actor.ZoneId,
            actor.IsBot,
            actor.DirX,
            actor.DirY)));

    public ValueTask<PlayerActor> TransferInAsync(
        string actorId,
        IZLinkActorContext context,
        ZLinkMessage state,
        CancellationToken cancellationToken)
    {
        var actor = new PlayerActor(actorId, context);
        if (!state.IsEmpty)
        {
            var transferred = state.Decode<PlayerTransferState>();
            actor.Restore(
                transferred.X,
                transferred.Y,
                transferred.ZoneId,
                transferred.IsBot,
                transferred.DirX,
                transferred.DirY);
        }

        return ValueTask.FromResult(actor);
    }

    private sealed record PlayerTransferState(
        int X,
        int Y,
        string ZoneId,
        bool IsBot,
        int DirX,
        int DirY);
}
