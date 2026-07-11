using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Actors;

internal static class ZLinkActorTransferRegistry
{
    public static ZLinkActorTransferRegistration CreateRegistration<TActor, TAdapter>()
        where TActor : IZLinkActor
        where TAdapter : class, IZLinkActorTransferAdapter<TActor>
    {
        return new ZLinkActorTransferRegistration(
            typeof(TActor),
            typeof(TAdapter),
            new ZLinkActorTransferInvoker<TActor>());
    }

    public static bool TryResolve(
        ZLinkFrameworkRegistration registration,
        string actorType,
        out ZLinkActorTransferRegistration? transfer)
    {
        return registration.ActorCatalog.TryGetTransfer(actorType, out transfer);
    }

    public static async ValueTask<ZLinkMessage> TransferOutAsync(
        IServiceProvider services,
        ZLinkActorTransferRegistration transfer,
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        await using var scope = services.CreateAsyncScope();
        var adapter = scope.ServiceProvider.GetRequiredService(transfer.AdapterType);
        return await transfer.Invoker
            .TransferOutAsync(adapter, actor, cancellationToken)
            .ConfigureAwait(false);
    }

    public static async ValueTask<IZLinkActor> TransferInAsync(
        IServiceProvider services,
        ZLinkActorTransferRegistration transfer,
        string actorId,
        IZLinkActorContext context,
        ZLinkMessage state,
        CancellationToken cancellationToken)
    {
        await using var scope = services.CreateAsyncScope();
        var adapter = scope.ServiceProvider.GetRequiredService(transfer.AdapterType);
        return await transfer.Invoker
            .TransferInAsync(adapter, actorId, context, state, cancellationToken)
            .ConfigureAwait(false);
    }

    private sealed class ZLinkActorTransferInvoker<TActor> : IZLinkActorTransferInvoker
        where TActor : IZLinkActor
    {
        public async ValueTask<ZLinkMessage> TransferOutAsync(
            object adapter,
            IZLinkActor actor,
            CancellationToken cancellationToken)
        {
            return await ((IZLinkActorTransferAdapter<TActor>)adapter)
                .TransferOutAsync((TActor)actor, cancellationToken)
                .ConfigureAwait(false);
        }

        public async ValueTask<IZLinkActor> TransferInAsync(
            object adapter,
            string actorId,
            IZLinkActorContext context,
            ZLinkMessage state,
            CancellationToken cancellationToken)
        {
            return await ((IZLinkActorTransferAdapter<TActor>)adapter)
                .TransferInAsync(actorId, context, state, cancellationToken)
                .ConfigureAwait(false);
        }
    }
}

internal interface IZLinkActorTransferInvoker
{
    ValueTask<ZLinkMessage> TransferOutAsync(
        object adapter,
        IZLinkActor actor,
        CancellationToken cancellationToken);

    ValueTask<IZLinkActor> TransferInAsync(
        object adapter,
        string actorId,
        IZLinkActorContext context,
        ZLinkMessage state,
        CancellationToken cancellationToken);
}
