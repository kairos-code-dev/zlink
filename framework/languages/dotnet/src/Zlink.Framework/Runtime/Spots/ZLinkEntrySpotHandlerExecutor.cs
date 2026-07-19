using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Runtime.Handlers;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkEntrySpotHandlerExecutor(
    IServiceProvider services,
    IZLinkEntrySpot entrySpot,
    string meshName,
    ZLinkCodecRegistryBuilder codecs,
    IZlinkStreamCompressionCodec? compressionCodec)
{
    public async ValueTask InvokeActorPacketAsync(
        ZLinkSpotActorPacketDescriptor descriptor,
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        await using var scope = services.CreateAsyncScope();
        await using var handlerInstances = new ZLinkScopedHandlerInstanceOwner(scope.ServiceProvider);
        var invoker = new ZLinkSpotHandlerInvoker(
            handlerInstances,
            entrySpot,
            meshName,
            codecs,
            compressionCodec);
        await invoker.InvokeActorPacketAsync(
                descriptor,
                actor,
                header,
                body,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<ZLinkActorReply> InvokeActorPacketForReplyAsync(
        ZLinkSpotActorPacketDescriptor descriptor,
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        await using var scope = services.CreateAsyncScope();
        await using var handlerInstances = new ZLinkScopedHandlerInstanceOwner(scope.ServiceProvider);
        var invoker = new ZLinkSpotHandlerInvoker(
            handlerInstances,
            entrySpot,
            meshName,
            codecs,
            compressionCodec);
        return await invoker.InvokeActorPacketForReplyAsync(
                descriptor,
                actor,
                header,
                body,
                cancellationToken)
            .ConfigureAwait(false);
    }
}
