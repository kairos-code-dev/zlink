using Zlink.Framework.Contracts.Actors;

namespace SupportChat.Server.Support.Infrastructure.ZLink.Actors;

internal sealed class SupportUserActorFactory : IZLinkActorFactory
{
    public ValueTask<IZLinkActor> CreateAsync(
        string actorId,
        IZLinkActorContext context,
        CancellationToken cancellationToken = default)
    {
        return ValueTask.FromResult<IZLinkActor>(new SupportUserActor(actorId, context));
    }
}
