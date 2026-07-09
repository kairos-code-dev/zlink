using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorCreationCoordinator(
    ZLinkFrameworkRuntime runtime,
    IServiceProvider services,
    Func<IZLinkBackendSpotNode?> getActorSpotNode,
    Func<ZLinkActorRuntimeState, ZLinkActorContext> ensureActorContext,
    Func<IZLinkActor, ZLinkActorRuntimeState, ZLinkActorContext> bindActorContext)
{
    private IZLinkActorLocationLifecycle? Lifecycle =>
        services.GetService(typeof(IZLinkActorLocationLifecycle)) as IZLinkActorLocationLifecycle;

    public async ValueTask<CreateActorResult> CreateAndBindActorAsync(
        ZLinkActorRuntimeState state,
        string actorId,
        string actorType,
        ZLinkMessage createRequest,
        bool failIfExists,
        ZLinkActorClaimMode claimMode,
        CancellationToken cancellationToken)
    {
        var actorNode = runtime.Registration.SpotNodes.Values
            .SingleOrDefault(static node => node.ActorFactories.Count > 0);
        if (actorNode is null
            || !actorNode.ActorFactories.TryGetValue(actorType, out var factoryType))
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorCreateFailed,
                $"Actor factory '{actorType}' is not registered.");

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
                    actorType,
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

        var node = getActorSpotNode();
        if (node is not null && state.NativeActorRef is null)
        {
            var existingRef = node.ActorLookup(actor.ActorId);
            if (existingRef is not null)
            {
                state.BindNativeActorRef(existingRef.Value);
            }
            else
            {
                using var nativeCreateRequest = createRequest.ToRawMessage(runtime.Registration.Codecs);
                state.BindNativeActorRef(node.CreateActor(actor.ActorId, nativeCreateRequest));
            }
        }

        state.EnsureActorGeneration(state.NativeActorRef?.Generation ?? 0);

        return actor;
    }
}
