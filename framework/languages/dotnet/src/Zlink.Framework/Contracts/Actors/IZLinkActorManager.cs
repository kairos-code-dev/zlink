namespace Zlink.Framework.Contracts.Actors;

public interface IZLinkActorManager
{
    /// <summary>
    /// Creates an actor with a globally unique actor id. The actor type string
    /// is used only by creation APIs and must match the value registered with
    /// AddActorFactory for the target factory.
    /// </summary>
    ValueTask<ActorRef> CreateAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default);

    /// <summary>
    /// Creates an actor with a creation payload. The actor type string is
    /// defined by AddActorFactory registration; lookup APIs use actor id only.
    /// </summary>
    ValueTask<ActorRef> CreateAsync(
        string actorId,
        string actorType,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken = default);

    ValueTask<ActorRef> CreateAsync<TRequest>(
        string actorId,
        string actorType,
        TRequest createRequest,
        CancellationToken cancellationToken = default)
    {
        return CreateAsync(
            actorId,
            actorType,
            ZLinkMessage.From(createRequest),
            cancellationToken);
    }

    /// <summary>
    /// Looks up an existing actor by its globally unique actor id. Actor type is
    /// not part of the lookup key.
    /// </summary>
    ValueTask<ActorRef?> FindAsync(
        string actorId,
        CancellationToken cancellationToken = default);

    /// <summary>
    /// Finds an actor by id or creates it using the factory registered for the
    /// supplied actor type string.
    /// </summary>
    ValueTask<ActorRef> GetOrCreateAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default);

    /// <summary>
    /// Finds an actor by id or creates it with a creation payload. The actor
    /// type string must be one registered with AddActorFactory.
    /// </summary>
    ValueTask<ActorRef> GetOrCreateAsync(
        string actorId,
        string actorType,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken = default);

    ValueTask<ActorRef> GetOrCreateAsync<TRequest>(
        string actorId,
        string actorType,
        TRequest createRequest,
        CancellationToken cancellationToken = default)
    {
        return GetOrCreateAsync(
            actorId,
            actorType,
            ZLinkMessage.From(createRequest),
            cancellationToken);
    }
}
