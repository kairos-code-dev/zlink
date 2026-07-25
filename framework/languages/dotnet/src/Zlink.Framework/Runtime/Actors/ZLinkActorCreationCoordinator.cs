using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorCreationCoordinator(
    ZLinkFrameworkRuntime runtime,
    IServiceProvider services,
    Func<IZLinkBackendSpotNode?> getActorSpotNode,
    IZLinkActorLocationLifecycle? lifecycle,
    Func<ZLinkActorRuntimeState, ZLinkActorContext> ensureActorContext,
    Func<IZLinkActor, ZLinkActorRuntimeState, ZLinkActorContext> bindActorContext,
    Func<ZLinkActorRuntimeState, ZLinkBackendActorRef, CancellationToken, ValueTask> teardownActor)
{
    private IZLinkActorLocationLifecycle? Lifecycle { get; } = lifecycle;

    public async ValueTask<CreateActorResult> CreateAndBindActorAsync(
        ZLinkActorRuntimeState state,
        string actorId,
        string actorType,
        ZLinkMessage createRequest,
        bool failIfExists,
        ZLinkActorClaimMode claimMode,
        CancellationToken cancellationToken)
    {
        var factoryType = ResolveActorFactory(actorType);

        var creation = await state.GetOrStartActorCreationAsync(
                actorType,
                failIfExists,
                () => CreateActorCoreAsync(
                    state,
                    actorId,
                    actorType,
                    factoryType,
                    createRequest,
                    claimMode,
                    CancellationToken.None).AsTask(),
                cancellationToken)
            .ConfigureAwait(false);

        // A waiter's cancellation never owns shared creation cleanup. The
        // state observes the shared task itself and clears only its failure.
        var actor = await creation.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
        return new CreateActorResult(
            actor,
            creation.Created,
            creation.Created ? createRequest : ZLinkMessage.Empty);
    }

    public async ValueTask<CreateActorResult> PrepareReservedActorAsync(
        ZLinkActorRuntimeState state,
        string actorId,
        string actorType,
        ZLinkMessage createRequest,
        ulong objectGeneration,
        ulong authorityOwnerGeneration,
        CancellationToken cancellationToken)
    {
        var factoryType = ResolveActorFactory(actorType);
        state.BeginReservedCreation();
        try
        {
            var creation = await state.GetOrStartActorCreationAsync(
                    actorType,
                    true,
                    () => ActivateActorCoreAsync(
                        state,
                        actorId,
                        factoryType,
                        createRequest,
                        CancellationToken.None,
                        objectGeneration,
                        authorityOwnerGeneration).AsTask(),
                    cancellationToken)
                .ConfigureAwait(false);
            var actor = await creation.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
            return new CreateActorResult(
                actor,
                creation.Created,
                creation.Created ? createRequest : ZLinkMessage.Empty);
        }
        catch
        {
            state.PublishReservedCreation();
            throw;
        }
    }

    public async ValueTask<CreateActorResult> TransferAndBindActorAsync(
        ZLinkActorRuntimeState state,
        string actorId,
        string actorType,
        ZLinkActorTransferRegistration? transfer,
        ZLinkMessage transferState,
        ulong objectGeneration,
        ulong authorityOwnerGeneration,
        ZLinkActorClaimMode claimMode,
        bool publishActorRef,
        CancellationToken cancellationToken)
    {
        var creation = await state.GetOrStartActorCreationAsync(
                actorType,
                false,
                () => TransferActorCoreAsync(
                    state,
                    actorId,
                    actorType,
                    transfer,
                    transferState,
                    objectGeneration,
                    authorityOwnerGeneration,
                    claimMode,
                    publishActorRef,
                    CancellationToken.None).AsTask(),
                cancellationToken)
            .ConfigureAwait(false);

        // A transfer creation belongs to the handoff transaction once it
        // starts. Observe it to a terminal result so cancellation cannot
        // detach a late actor/claim from rollback ownership.
        var actor = await creation.Task.ConfigureAwait(false);
        return new CreateActorResult(actor, creation.Created, ZLinkMessage.Empty);
    }

    private async ValueTask<IZLinkActor> TransferActorCoreAsync(
        ZLinkActorRuntimeState state,
        string actorId,
        string actorType,
        ZLinkActorTransferRegistration? transfer,
        ZLinkMessage transferState,
        ulong objectGeneration,
        ulong authorityOwnerGeneration,
        ZLinkActorClaimMode claimMode,
        bool publishActorRef,
        CancellationToken cancellationToken)
    {
        if (transfer is null)
        {
            var factoryType = ResolveActorFactory(actorType);

            return await CreateActorCoreAsync(
                    state,
                    actorId,
                    actorType,
                    factoryType,
                    ZLinkMessage.Empty,
                    claimMode,
                    cancellationToken,
                    publishActorRef,
                    objectGeneration,
                    authorityOwnerGeneration)
                .ConfigureAwait(false);
        }

        return await CreateTransferredActorCoreAsync(
                state,
                actorId,
                actorType,
            transfer,
            transferState,
            objectGeneration,
            authorityOwnerGeneration,
            claimMode,
                cancellationToken,
                publishActorRef)
            .ConfigureAwait(false);
    }

    private Type ResolveActorFactory(string actorType)
    {
        return runtime.Registration.ActorCatalog.ResolveFactory(actorType);
    }

    private async ValueTask<IZLinkActor> CreateTransferredActorCoreAsync(
        ZLinkActorRuntimeState state,
        string actorId,
        string actorType,
        ZLinkActorTransferRegistration transfer,
        ZLinkMessage transferState,
        ulong objectGeneration,
        ulong authorityOwnerGeneration,
        ZLinkActorClaimMode claimMode,
        CancellationToken cancellationToken,
        bool publishActorRef)
    {
        if (Lifecycle is not { } lifecycle)
            return await ActivateTransferredActorCoreAsync(
                    state,
                    actorId,
                    transfer,
                    transferState,
                    objectGeneration,
                    authorityOwnerGeneration,
                    cancellationToken)
                .ConfigureAwait(false);

        var outcome = await lifecycle.ExecuteActorClaimThenActivateAsync(
                Host.ZLinkActorDrainCoordinator.ResolveMeshName(runtime.Registration, actorType)
                    ?? string.Empty,
                actorType,
                actorId,
                getActorSpotNode()?.RoutingId ?? default,
                deactivate: _ => runtime.DeactivateActorOnOwnershipLossAsync(actorId),
                activate: ct => ActivateTransferredActorCoreAsync(
                    state,
                    actorId,
                    transfer,
                    transferState,
                    objectGeneration,
                    authorityOwnerGeneration,
                    ct),
                cancellationToken,
                claimMode)
            .ConfigureAwait(false);
        if (outcome.Activated is not { } actor)
        {
            var location = outcome.ExistingLocation;
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorCreateFailed,
                location is null
                    ? $"Actor '{actorId}' location claim was rejected and no live location row was found."
                    : $"Actor '{actorId}' is already active on node '{location.OwnerNodeRid}' (location claim conflict).");
        }

        if (publishActorRef && state.NativeActorRef is { } nativeRef)
            await PublishActorRefOrCompensateAsync(
                    state,
                    actorId,
                    nativeRef,
                    lifecycle,
                    cancellationToken)
                .ConfigureAwait(false);

        return actor;
    }

    private async ValueTask<IZLinkActor> ActivateTransferredActorCoreAsync(
        ZLinkActorRuntimeState state,
        string actorId,
        ZLinkActorTransferRegistration transfer,
        ZLinkMessage transferState,
        ulong objectGeneration,
        ulong authorityOwnerGeneration,
        CancellationToken cancellationToken)
    {
        var context = ensureActorContext(state);
        var actor = await ZLinkActorTransferRegistry.TransferInAsync(
                services,
                transfer,
                actorId,
                context,
                transferState,
                cancellationToken)
            .ConfigureAwait(false);
        if (actor is null) throw new InvalidOperationException($"Actor transfer adapter '{transfer.AdapterType}' returned null.");

        if (!string.Equals(actor.ActorId, actorId, StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"Actor transfer adapter '{transfer.AdapterType}' returned actor id '{actor.ActorId}' for requested id '{actorId}'.");

        bindActorContext(actor, state);
        EnsureNativeActorRef(
            state,
            actor.ActorId,
            ZLinkMessage.Empty,
            reservedGeneration: objectGeneration,
            reservedAuthorityOwnerGeneration: authorityOwnerGeneration);

        return actor;
    }

    private async ValueTask<IZLinkActor> CreateActorCoreAsync(
        ZLinkActorRuntimeState state,
        string actorId,
        string actorType,
        Type factoryType,
        ZLinkMessage createRequest,
        ZLinkActorClaimMode claimMode,
        CancellationToken cancellationToken,
        bool publishActorRef = true,
        ulong? reservedGeneration = null,
        ulong? reservedAuthorityOwnerGeneration = null)
    {
        if (Lifecycle is not { } lifecycle)
            return await ActivateActorCoreAsync(
                    state,
                    actorId,
                    factoryType,
                    createRequest,
                    cancellationToken,
                    reservedGeneration,
                    reservedAuthorityOwnerGeneration)
                .ConfigureAwait(false);

        // Claim-then-activate (location resolver store draft, section 17):
        // the actor location claim must succeed before any instance exists.
        // A losing claimer backs off without activating.
        var outcome = await lifecycle.ExecuteActorClaimThenActivateAsync(
                Host.ZLinkActorDrainCoordinator.ResolveMeshName(runtime.Registration, actorType)
                    ?? string.Empty,
                actorType,
                actorId,
                getActorSpotNode()?.RoutingId ?? default,
                deactivate: _ => runtime.DeactivateActorOnOwnershipLossAsync(actorId),
                activate: ct => ActivateActorCoreAsync(
                    state,
                    actorId,
                    factoryType,
                    createRequest,
                    ct,
                    reservedGeneration,
                    reservedAuthorityOwnerGeneration),
                cancellationToken,
                claimMode)
            .ConfigureAwait(false);
        if (outcome.Activated is not { } actor)
        {
            var location = outcome.ExistingLocation;
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorCreateFailed,
                location is null
                    ? $"Actor '{actorId}' location claim was rejected and no live location row was found."
                    : $"Actor '{actorId}' is already active on node '{location.OwnerNodeRid}' (location claim conflict).");
        }

        if (publishActorRef && state.NativeActorRef is { } nativeRef)
            await PublishActorRefOrCompensateAsync(
                    state,
                    actorId,
                    nativeRef,
                    lifecycle,
                    cancellationToken)
                .ConfigureAwait(false);

        return actor;
    }

    private async ValueTask<IZLinkActor> ActivateActorCoreAsync(
        ZLinkActorRuntimeState state,
        string actorId,
        Type factoryType,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken,
        ulong? reservedGeneration = null,
        ulong? reservedAuthorityOwnerGeneration = null)
    {
        await using var scope = services.CreateAsyncScope();
        var context = ensureActorContext(state);
        var factory = (IZLinkActorFactory)scope.ServiceProvider.GetRequiredService(factoryType);
        var actor = await factory.CreateAsync(actorId, context, cancellationToken)
            .ConfigureAwait(false);
        if (actor is null) throw new InvalidOperationException($"Actor factory '{factoryType}' returned null.");

        if (!string.Equals(actor.ActorId, actorId, StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"Actor factory '{factoryType}' returned actor id '{actor.ActorId}' for requested id '{actorId}'.");

        bindActorContext(actor, state);
        EnsureNativeActorRef(
            state,
            actor.ActorId,
            createRequest,
            reservedGeneration,
            reservedAuthorityOwnerGeneration);

        return actor;
    }

    private void EnsureNativeActorRef(
        ZLinkActorRuntimeState state,
        string actorId,
        ZLinkMessage createRequest,
        ulong? reservedGeneration = null,
        ulong? reservedAuthorityOwnerGeneration = null)
    {
        var node = getActorSpotNode();
        if (node is null || state.NativeActorRef is not null) return;

        var existingRef = node.ActorLookup(actorId);
        if (existingRef is not null)
        {
            state.BindNativeActorRef(existingRef.Value);
            return;
        }

        using var nativeCreateRequest = createRequest.ToRawMessage(runtime.Registration.Codecs);
        state.BindNativeActorRef(reservedGeneration is { } generation
            ? node.CreateReservedActor(
                actorId,
                generation,
                reservedAuthorityOwnerGeneration
                ?? throw new InvalidOperationException(
                    "Reserved Actor creation requires an authority owner generation."),
                nativeCreateRequest)
            : node.CreateActor(actorId, nativeCreateRequest));
    }

    private async ValueTask PublishActorRefOrCompensateAsync(
        ZLinkActorRuntimeState state,
        string actorId,
        ZLinkBackendActorRef nativeActor,
        IZLinkActorLocationLifecycle lifecycle,
        CancellationToken cancellationToken)
    {
        try
        {
            await lifecycle.PublishActorRefAsync(
                    actorId,
                    nativeActor.ToNative(),
                    cancellationToken)
                .ConfigureAwait(false);
        }
        catch (Exception publishFailure)
        {
            await state.ExecuteLockedAsync(
                    state.BeginTeardown,
                    CancellationToken.None)
                .ConfigureAwait(false);

            try
            {
                await teardownActor(state, nativeActor, CancellationToken.None)
                    .ConfigureAwait(false);
            }
            catch (Exception cleanupFailure)
            {
                runtime.RunDetached(
                    "actor-creation-compensation",
                    ct => ReconcileCreationCompensationAsync(
                        state,
                        actorId,
                        nativeActor,
                        ct));
                throw new AggregateException(publishFailure, cleanupFailure);
            }
            throw;
        }
    }

    private async ValueTask ReconcileCreationCompensationAsync(
        ZLinkActorRuntimeState state,
        string actorId,
        ZLinkBackendActorRef nativeActor,
        CancellationToken cancellationToken)
    {
        await ZLinkReconciliationRunner.RunAsync(
                token => teardownActor(state, nativeActor, token),
                exception => ZLinkFrameworkDebugLog.SpotDiscovery(
                    $"actor creation compensation retry for '{actorId}': {exception.Message}"),
                cancellationToken,
                static exception => exception is OperationCanceledException)
            .ConfigureAwait(false);
    }
}
