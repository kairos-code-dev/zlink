using Systems.Zlink.Stream.Connector.Protocol;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotActorPacketDispatcher(
    ZLinkFrameworkRuntime runtime,
    Func<ZLinkSpotActorHandlerRegistry?> actorHandlers,
    Func<ZLinkSpotHandlerInvoker> handlerInvoker)
{
    public async ValueTask DispatchAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        using var dispatch = runtimeState.EnterDispatch(header);
        if (TryResolveActorPacketDescriptor(actor.GetType(), header, out var descriptor)
            && descriptor is not null)
        {
            await handlerInvoker()
                .InvokeActorPacketAsync(descriptor, actor, header, body, cancellationToken)
                .ConfigureAwait(false);
            return;
        }

        await runtimeState.DispatchAsync(
                runtime.Services,
                actor,
                header,
                body.Move(),
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<byte[]?> DispatchForReplyAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        using var dispatch = runtimeState.EnterDispatch(header);
        if (TryResolveActorPacketDescriptor(actor.GetType(), header, out var descriptor)
            && descriptor is not null)
        {
            return await handlerInvoker()
                .InvokeActorPacketForReplyAsync(descriptor, actor, header, body, cancellationToken)
                .ConfigureAwait(false);
        }

        return await runtimeState.DispatchForReplyAsync(
                runtime.Services,
                actor,
                header,
                body.Move(),
                cancellationToken)
            .ConfigureAwait(false);
    }

    private bool TryResolveActorPacketDescriptor(
        Type actorType,
        ZlinkStreamHeader header,
        out ZLinkSpotActorPacketDescriptor? descriptor)
    {
        descriptor = null;
        return actorHandlers() is { } handlers
            && handlers.TryResolve(actorType, header, out descriptor);
    }
}
