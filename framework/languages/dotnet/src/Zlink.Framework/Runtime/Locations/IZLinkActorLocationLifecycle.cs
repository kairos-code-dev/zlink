namespace Zlink.Framework.Runtime.Locations;

internal interface IZLinkActorLocationLifecycle
{
    ValueTask<ZLinkActorClaimActivation<TActor>> ExecuteActorClaimThenActivateAsync<TActor>(
        string actorType,
        string actorId,
        RoutingId nodeRid,
        Func<CancellationToken, ValueTask>? deactivate,
        Func<CancellationToken, ValueTask<TActor>> activate,
        CancellationToken cancellationToken,
        ZLinkActorClaimMode claimMode = ZLinkActorClaimMode.NewOwner)
        where TActor : class;

    ValueTask<ZLinkActorClaimResult> ClaimActorAsync(
        string actorType,
        string actorId,
        RoutingId nodeRid,
        Func<CancellationToken, ValueTask>? deactivate,
        CancellationToken cancellationToken);

    ValueTask<ZLinkLocationWriteResult> PublishActorRefAsync(
        string actorId,
        ActorRef actorRef,
        CancellationToken cancellationToken = default);
}
