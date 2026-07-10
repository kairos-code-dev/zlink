using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Actors;

internal static class ZLinkActorTransferRegistry
{
    public static bool TryResolve(
        ZLinkFrameworkRegistration registration,
        string actorType,
        out ZLinkActorTransferRegistration? transfer)
    {
        foreach (var spotNode in registration.SpotNodes.Values)
        {
            if (spotNode.ActorTransfers.TryGetValue(actorType, out transfer!))
                return true;
        }

        transfer = null;
        return false;
    }

    public static async ValueTask<ZLinkMessage> TransferOutAsync(
        IServiceProvider services,
        ZLinkActorTransferRegistration transfer,
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        await using var scope = services.CreateAsyncScope();
        var adapter = scope.ServiceProvider.GetRequiredService(transfer.AdapterType);
        var invokerType = typeof(ZLinkActorTransferInvoker<>).MakeGenericType(transfer.ActorType);
        return await ((IZLinkActorTransferOutInvoker)Activator.CreateInstance(invokerType)!)
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
        var invokerType = typeof(ZLinkActorTransferInvoker<>).MakeGenericType(transfer.ActorType);
        return await ((IZLinkActorTransferInInvoker)Activator.CreateInstance(invokerType)!)
            .TransferInAsync(adapter, actorId, context, state, cancellationToken)
            .ConfigureAwait(false);
    }

    private interface IZLinkActorTransferOutInvoker
    {
        ValueTask<ZLinkMessage> TransferOutAsync(
            object adapter,
            IZLinkActor actor,
            CancellationToken cancellationToken);
    }

    private interface IZLinkActorTransferInInvoker
    {
        ValueTask<IZLinkActor> TransferInAsync(
            object adapter,
            string actorId,
            IZLinkActorContext context,
            ZLinkMessage state,
            CancellationToken cancellationToken);
    }

    private sealed class ZLinkActorTransferInvoker<TActor> :
        IZLinkActorTransferOutInvoker,
        IZLinkActorTransferInInvoker
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
