using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorCreationCoordinator(
    ZLinkFrameworkRuntime runtime,
    IServiceProvider services,
    Func<IZLinkBackendSpotNode?> getActorSpotNode,
    IZLinkActorLocationLifecycle? lifecycle,
    Func<ZLinkActorRuntimeState, ZLinkActorContext> ensureActorContext,
    Func<IZLinkActor, ZLinkActorRuntimeState, ZLinkActorContext> bindActorContext)
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

        try
        {
            var actor = await creation.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
            return new CreateActorResult(
                actor,
                creation.Created,
                creation.Created ? createRequest : ZLinkMessage.Empty);
        }
        catch
        {
            await state.ClearFailedActorCreationAsync(creation.Task)
                .ConfigureAwait(false);
            throw;
        }
    }

    public async ValueTask<CreateActorResult> TransferAndBindActorAsync(
        ZLinkActorRuntimeState state,
        string actorId,
        string actorType,
        ZLinkActorTransferRegistration? transfer,
        ZLinkMessage transferState,
        ZLinkActorClaimMode claimMode,
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
                    claimMode,
                    CancellationToken.None).AsTask(),
                cancellationToken)
            .ConfigureAwait(false);

        try
        {
            var actor = await creation.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
            return new CreateActorResult(actor, creation.Created, ZLinkMessage.Empty);
        }
        catch
        {
            await state.ClearFailedActorCreationAsync(creation.Task)
                .ConfigureAwait(false);
            throw;
        }
    }

    private async ValueTask<IZLinkActor> TransferActorCoreAsync(
        ZLinkActorRuntimeState state,
        string actorId,
        string actorType,
        ZLinkActorTransferRegistration? transfer,
        ZLinkMessage transferState,
        ZLinkActorClaimMode claimMode,
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
                    cancellationToken)
                .ConfigureAwait(false);
        }

        return await CreateTransferredActorCoreAsync(
                state,
                actorId,
                actorType,
                transfer,
                transferState,
                claimMode,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private Type ResolveActorFactory(string actorType)
    {
        foreach (var actorNode in runtime.Registration.SpotNodes.Values)
        {
            if (actorNode.ActorFactories.TryGetValue(actorType, out var factoryType))
                return factoryType;
        }

        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ActorCreateFailed,
            $"Actor factory '{actorType}' is not registered.");
    }

    private async ValueTask<IZLinkActor> CreateTransferredActorCoreAsync(
        ZLinkActorRuntimeState state,
        string actorId,
        string actorType,
        ZLinkActorTransferRegistration transfer,
        ZLinkMessage transferState,
        ZLinkActorClaimMode claimMode,
        CancellationToken cancellationToken)
    {
        if (Lifecycle is not { } lifecycle)
            return await ActivateTransferredActorCoreAsync(state, actorId, transfer, transferState, cancellationToken)
                .ConfigureAwait(false);

        var outcome = await lifecycle.ExecuteActorClaimThenActivateAsync(
                actorType,
                actorId,
                getActorSpotNode()?.RoutingId ?? default,
                deactivate: _ => runtime.DeactivateActorOnOwnershipLossAsync(actorId),
                activate: ct => ActivateTransferredActorCoreAsync(state, actorId, transfer, transferState, ct),
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
                    : $"Actor '{actorId}' is already active on node '{location.NodeRid}' (location claim conflict).");
        }

        if (state.NativeActorRef is { } nativeRef)
            await lifecycle.PublishActorRefAsync(
                    actorId,
                    nativeRef.ToNative(),
                    cancellationToken)
                .ConfigureAwait(false);

        return actor;
    }

    private async ValueTask<IZLinkActor> ActivateTransferredActorCoreAsync(
        ZLinkActorRuntimeState state,
        string actorId,
        ZLinkActorTransferRegistration transfer,
        ZLinkMessage transferState,
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
        EnsureNativeActorRef(state, actor.ActorId, ZLinkMessage.Empty);

        return actor;
    }

    private async ValueTask<IZLinkActor> CreateActorCoreAsync(
        ZLinkActorRuntimeState state,
        string actorId,
        string actorType,
        Type factoryType,
        ZLinkMessage createRequest,
        ZLinkActorClaimMode claimMode,
        CancellationToken cancellationToken)
    {
        if (Lifecycle is not { } lifecycle)
            return await ActivateActorCoreAsync(state, actorId, factoryType, createRequest, cancellationToken)
                .ConfigureAwait(false);

        // Claim-then-activate (location resolver store draft, section 17):
        // the actor location claim must succeed before any instance exists.
        // A losing claimer backs off without activating.
        var outcome = await lifecycle.ExecuteActorClaimThenActivateAsync(
                actorType,
                actorId,
                getActorSpotNode()?.RoutingId ?? default,
                deactivate: _ => runtime.DeactivateActorOnOwnershipLossAsync(actorId),
                activate: ct => ActivateActorCoreAsync(state, actorId, factoryType, createRequest, ct),
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
                    : $"Actor '{actorId}' is already active on node '{location.NodeRid}' (location claim conflict).");
        }

        if (state.NativeActorRef is { } nativeRef)
            await lifecycle.PublishActorRefAsync(
                    actorId,
                    nativeRef.ToNative(),
                    cancellationToken)
                .ConfigureAwait(false);

        return actor;
    }

    private async ValueTask<IZLinkActor> ActivateActorCoreAsync(
        ZLinkActorRuntimeState state,
        string actorId,
        Type factoryType,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken)
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
        EnsureNativeActorRef(state, actor.ActorId, createRequest);

        return actor;
    }

    private void EnsureNativeActorRef(
        ZLinkActorRuntimeState state,
        string actorId,
        ZLinkMessage createRequest)
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
        state.BindNativeActorRef(node.CreateActor(actorId, nativeCreateRequest));
    }
}
