
using Zlink.Framework.Runtime.Streams;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotActorPacketDispatcher(
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
    }

    public async ValueTask<ZLinkActorReply?> DispatchForReplyAsync(
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

        return null;
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
