using DeliveryDispatch.Shared.Contracts;
using Zlink.Framework.Contracts.Handlers;

namespace DeliveryDispatch.Server.DispatchCenter;

[ZLinkHandlerGroup("dispatch")]
internal sealed class AssignDeliveryHandler(DispatchWorkQueue queue)
    : IZLinkRequestHandler<AssignDelivery, AssignDeliveryResult>
{
    public async ValueTask<AssignDeliveryResult> HandleAsync(
        AssignDelivery request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        await queue.EnqueueAsync(request, cancellationToken);
        Console.Error.WriteLine($"deliverydispatch dispatch: queued delivery={request.DeliveryId} customer={request.CustomerId}");
        return new AssignDeliveryResult(request.DeliveryId, "pending");
    }
}
