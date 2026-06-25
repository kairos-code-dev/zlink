using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorCreationCoordinator(
    ZLinkFrameworkRuntime runtime,
    IServiceProvider services,
    Func<IZLinkBackendSpotNode?> getActorSpotNode,
    Func<ZLinkActorRuntimeState, ZLinkActorContext> ensureActorContext,
    Func<IZLinkActor, ZLinkActorRuntimeState, ZLinkActorContext> bindActorContext)
{
    public async ValueTask<CreateActorResult> CreateAndBindActorAsync(
        ZLinkActorRuntimeState state,
        string actorId,
        string actorType,
        ZLinkMessage createRequest,
        bool failIfExists,
        CancellationToken cancellationToken)
    {
        var actorNode = runtime.Registration.SpotNodes.Values
            .SingleOrDefault(static node => node.ActorFactories.Count > 0);
        if (actorNode is null
            || !actorNode.ActorFactories.TryGetValue(actorType, out var factoryType))
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorCreateFailed,
                $"Actor factory '{actorType}' is not registered.");
        }

        var creation = await state.GetOrStartActorCreationAsync(
                actorType,
                failIfExists,
                () => CreateActorCoreAsync(
                    state,
                    actorId,
                    factoryType,
                    createRequest,
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
        Type factoryType,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken)
    {
        await using var scope = services.CreateAsyncScope();
        var context = ensureActorContext(state);
        var factory = (IZLinkActorFactory)scope.ServiceProvider.GetRequiredService(factoryType);
        var actor = await factory.CreateAsync(actorId, context, cancellationToken)
            .ConfigureAwait(false);
        if (actor is null)
        {
            throw new InvalidOperationException($"Actor factory '{factoryType}' returned null.");
        }

        if (!string.Equals(actor.ActorId, actorId, StringComparison.Ordinal))
        {
            throw new InvalidOperationException(
                $"Actor factory '{factoryType}' returned actor id '{actor.ActorId}' for requested id '{actorId}'.");
        }

        bindActorContext(actor, state);

        var node = getActorSpotNode();
        if (node is not null && state.NativeActorRef is null)
        {
            var existingRef = node.ActorLookup(actor.ActorId);
            if (existingRef is not null)
            {
                state.NativeActorRef = existingRef;
            }
            else
            {
                using var nativeCreateRequest = createRequest.ToRawMessage(runtime.Registration.Codecs);
                state.NativeActorRef = node.CreateActor(actor.ActorId, nativeCreateRequest);
            }
        }

        state.EnsureActorGeneration(state.NativeActorRef?.Generation ?? 0);

        return actor;
    }
}
