using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Spots;

namespace YieldDispatch.Server.Session.Support;

internal sealed class SessionYieldEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot<SessionYieldActor>
{
    public IZLinkEntrySpotContext Context { get; } = context;
}

internal sealed class SessionYieldActorFactory : IZLinkActorFactory
{
    public ValueTask<IZLinkActor> CreateAsync(
        string actorId,
        IZLinkActorContext context,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult<IZLinkActor>(new SessionYieldActor(actorId, context));
    }
}

internal sealed class SessionYieldActor(string actorId, IZLinkActorContext context) : IZLinkActor
{
    public string ActorId { get; } = actorId;

    public IZLinkActorContext Context { get; } = context;
}