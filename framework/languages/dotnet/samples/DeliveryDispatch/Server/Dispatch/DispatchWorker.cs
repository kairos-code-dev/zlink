using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Channels;

namespace DeliveryDispatch.Server.Dispatch;

internal sealed class DispatchWorker(
    DispatchWorkQueue queue,
    IZLinkChannelClient channels,
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
        AssignDelivery request,
        CancellationToken cancellationToken)
    {
        logger.LogInformation(
            "deliverydispatch dispatch: assign delivery={DeliveryId} customer={CustomerId}",
            request.DeliveryId,
            request.CustomerId);
        await PublishStatusAsync(request.DeliveryId, DeliveryStatus.Assigned, "courier-a", cancellationToken);

        var first = await TryOfferAsync(request, "courier-a", cancellationToken);
        if (first.Accepted)
        {
            await ContinueAcceptedDeliveryAsync(request.DeliveryId, first.CourierId, cancellationToken);
            return;
        }

        await PublishStatusAsync(request.DeliveryId, DeliveryStatus.Reassigned, "courier-b", cancellationToken);
        var second = await TryOfferAsync(request, "courier-b", cancellationToken);
        if (!second.Accepted)
        {
            await PublishStatusAsync(request.DeliveryId, DeliveryStatus.Failed, second.CourierId, cancellationToken);
            throw new InvalidOperationException($"Delivery '{request.DeliveryId}' was rejected by all couriers.");
        }

        await ContinueAcceptedDeliveryAsync(request.DeliveryId, second.CourierId, cancellationToken);
    }

    private async ValueTask<OfferDeliveryResult> TryOfferAsync(
        AssignDelivery request,
        string courierId,
        CancellationToken cancellationToken)
    {
        try
        {
            return await RequestChannelWithRetryAsync<OfferDelivery, OfferDeliveryResult>(
                SampleNames.CourierRouteChannel,
                new OfferDelivery(courierId, request.DeliveryId, request.PickupAddress, request.DropoffAddress),
                cancellationToken,
                SampleTimings.OfferRequestTimeout,
                maxAttempts: 8);
        }
        catch (Exception error) when (error is not OperationCanceledException)
        {
            logger.LogWarning(
                error,
                "deliverydispatch dispatch: courier timeout delivery={DeliveryId} courier={CourierId}",
                request.DeliveryId,
                courierId);
            return new OfferDeliveryResult(request.DeliveryId, courierId, Accepted: false, error.Message);
        }
    }

    private async ValueTask ContinueAcceptedDeliveryAsync(
        string deliveryId,
        string courierId,
        CancellationToken cancellationToken)
    {
        await PublishStatusAsync(deliveryId, DeliveryStatus.Accepted, courierId, cancellationToken);
        await PublishStatusAsync(deliveryId, DeliveryStatus.PickedUp, courierId, cancellationToken);
        await PublishStatusAsync(deliveryId, DeliveryStatus.Delivered, courierId, cancellationToken);
    }

    private async ValueTask PublishStatusAsync(
        string deliveryId,
        DeliveryStatus status,
        string? courierId,
        CancellationToken cancellationToken)
    {
        _ = await RequestChannelWithRetryAsync<DeliveryStatusChanged, DeliveryStatusAck>(
            SampleNames.TrackingRouteChannel,
            new DeliveryStatusChanged(deliveryId, status, courierId, DateTimeOffset.UtcNow),
            cancellationToken);
    }

    private async ValueTask<TReply> RequestChannelWithRetryAsync<TRequest, TReply>(
        string channelName,
        TRequest request,
        CancellationToken cancellationToken,
        TimeSpan? timeout = null,
        int maxAttempts = 40)
    {
        Exception? lastError = null;
        for (var attempt = 1; attempt <= maxAttempts; attempt++)
        {
            try
            {
                var call = channels.RequestToChannel(channelName, request);
                if (timeout is { } value)
                {
                    call = call.Timeout(value);
                }

                return await call.Async<TReply>(cancellationToken);
            }
            catch (Exception error) when (error is not OperationCanceledException)
            {
                lastError = error;
                if (attempt == 1 || attempt == maxAttempts)
                {
                    logger.LogWarning(
                        error,
                        "deliverydispatch dispatch: channel wait channel={ChannelName} request={RequestName} attempt={Attempt}",
                        channelName,
                        typeof(TRequest).Name,
                        attempt);
                }
                await Task.Delay(100, cancellationToken);
            }
        }

        throw new InvalidOperationException(
            $"Channel '{channelName}' was not ready for request '{typeof(TRequest).Name}'.",
            lastError);
    }
}
