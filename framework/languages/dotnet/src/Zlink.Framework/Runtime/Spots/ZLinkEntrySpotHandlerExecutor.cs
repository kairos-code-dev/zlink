using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkEntrySpotHandlerExecutor(
    IServiceProvider services,
    IZLinkEntrySpot entrySpot,
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
        var invoker = new ZLinkSpotHandlerInvoker(scope.ServiceProvider, entrySpot, codecs, compressionCodec);
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
        var invoker = new ZLinkSpotHandlerInvoker(scope.ServiceProvider, entrySpot, codecs, compressionCodec);
        return await invoker.InvokeActorPacketForReplyAsync(
                descriptor,
                actor,
                header,
                body,
                cancellationToken)
            .ConfigureAwait(false);
    }
}