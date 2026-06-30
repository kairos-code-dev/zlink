using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Spots;

namespace YieldDispatch.Server.Play.Spots;

internal sealed class YieldEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot<YieldActor>
{
    public IZLinkEntrySpotContext Context { get; } = context;
}

internal sealed class YieldActorFactory : IZLinkActorFactory
{
    public ValueTask<IZLinkActor> CreateAsync(
        string actorId,
        IZLinkActorContext context,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult<IZLinkActor>(new YieldActor(actorId, context));
    }
}

internal sealed class YieldActor(string actorId, IZLinkActorContext context) : IZLinkActor
{
    public string ActorId { get; } = actorId;

    public IZLinkActorContext Context { get; } = context;
}