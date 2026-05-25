namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorManagerService(ZLinkFrameworkRuntime runtime) : IZLinkActorManager
{
    public async ValueTask<IZLinkActor> CreateAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default)
    {
        var result = await runtime.CreateActorAsync(actorId, actorType, cancellationToken)
            .ConfigureAwait(false);
        return result.Actor;
    }

    public async ValueTask<IZLinkActor?> FindAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        return await runtime.FindActorAsync(actorId, cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<IZLinkActor> GetOrCreateAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default)
    {
        var result = await runtime.CreateLocalActorAsync(actorId, actorType, cancellationToken)
            .ConfigureAwait(false);
        return result.Actor;
    }
}
