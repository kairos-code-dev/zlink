using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;

namespace DeliveryDispatch.Server.Dispatch;

internal sealed class DispatchWorker(
    DispatchWorkQueue queue,
    CourierOfferPort courierOffers,
    DeliveryStatusPublisher statusPublisher,
    ILogger<DispatchWorker> logger)
    : BackgroundService
{
    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        await foreach (var request in queue.ReadAllAsync(stoppingToken))
        {
            await DispatchAsync(request, stoppingToken);
        }
    }

    private async ValueTask DispatchAsync(
        AssignDeliveryMsg request,
        CancellationToken cancellationToken)
    {
        logger.LogInformation(
            "deliverydispatch dispatch: assign delivery={DeliveryId} customer={CustomerId}",
            request.DeliveryId,
            request.CustomerId);
        var first = await courierOffers.OfferAsync(request, "courier-a", cancellationToken);
        if (!first.Delivered)
        {
            throw new InvalidOperationException($"Delivery '{request.DeliveryId}' could not be offered to courier-a.");
        }

        await statusPublisher.PublishAsync(request, DeliveryStatus.Assigned, first.Response.CourierId, cancellationToken);
        if (first.Response.Accepted)
        {
            await ContinueAcceptedDeliveryAsync(request, first.Response.CourierId, includePickup: true, cancellationToken);
            return;
        }

        await statusPublisher.PublishAsync(request, DeliveryStatus.Reassigned, "courier-b", cancellationToken);
        var second = await courierOffers.OfferAsync(request, "courier-b", cancellationToken);
        if (!second.Delivered || !second.Response.Accepted)
        {
            await statusPublisher.PublishAsync(request, DeliveryStatus.Failed, "courier-b", cancellationToken);
            throw new InvalidOperationException($"Delivery '{request.DeliveryId}' was rejected by all couriers.");
        }

        await ContinueAcceptedDeliveryAsync(request, second.Response.CourierId, includePickup: false, cancellationToken);
    }

    private async ValueTask ContinueAcceptedDeliveryAsync(
        AssignDeliveryMsg request,
        string courierId,
        bool includePickup,
        CancellationToken cancellationToken)
    {
        await statusPublisher.PublishAsync(request, DeliveryStatus.Accepted, courierId, cancellationToken);
        if (includePickup)
        {
            await statusPublisher.PublishAsync(request, DeliveryStatus.PickedUp, courierId, cancellationToken);
        }
        await statusPublisher.PublishAsync(request, DeliveryStatus.Delivered, courierId, cancellationToken);
    }
}

internal sealed record DispatchOfferAttempt(
    OfferDeliveryRes Response,
    bool Delivered)
{
    public static DispatchOfferAttempt NotDelivered(
        string deliveryId,
        string courierId,
        string? reason)
    {
        return new DispatchOfferAttempt(
            new OfferDeliveryRes(deliveryId, courierId, Accepted: false, reason),
            Delivered: false);
    }
}
