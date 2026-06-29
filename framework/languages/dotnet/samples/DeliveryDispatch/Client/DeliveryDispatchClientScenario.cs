using System.Runtime.CompilerServices;
using DeliveryDispatch.Shared.Contracts;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

namespace DeliveryDispatch.Client;

internal sealed class DeliveryDispatchClientScenario
{
    public async ValueTask RunAsync(
        ZLinkHttpClient http,
        IZlinkStreamConnector customer,
        IZlinkStreamConnector courier,
        CancellationToken cancellationToken = default)
    {
        await customer.Connect.Async(cancellationToken);
        await courier.Connect.Async(cancellationToken);

        _ = await BindCouriersAsync(courier, cancellationToken);

        await RunSuccessfulDeliveryAsync(http, customer, courier, cancellationToken);
        await RunReassignedDeliveryAsync(http, customer, courier, cancellationToken);
        await AssertServerEvidenceAsync(http, cancellationToken);
    }

    private static async ValueTask<CouriersBound> BindCouriersAsync(
        IZlinkStreamConnector courier,
        CancellationToken cancellationToken)
    {
        Exception? lastError = null;
        for (var attempt = 1; attempt <= 40; attempt++)
        {
            try
            {
                return await courier.Request(new BindCouriers(["courier-a", "courier-b"]))
                    .Async<CouriersBound>(cancellationToken);
            }
            catch (Exception error) when (error is not OperationCanceledException)
            {
                lastError = error;
                await Task.Delay(100, cancellationToken);
            }
        }

        throw new InvalidOperationException("Courier stream bind failed.", lastError);
    }

    private static async ValueTask RunSuccessfulDeliveryAsync(
        ZLinkHttpClient http,
        IZlinkStreamConnector customer,
        IZlinkStreamConnector courier,
        CancellationToken cancellationToken)
    {
        var deliveryId = "delivery-success";
        var offer = courier.WaitFor<OfferDelivery>()
            .Where(message => message.Payload.DeliveryId == deliveryId)
            .Async(cancellationToken);
        var assigned = customer.WaitFor<DeliveryStatusNotify>()
            .Where(message => message.Payload.DeliveryId == deliveryId && message.Payload.Status == DeliveryStatus.Assigned)
            .Async(cancellationToken);
        var accepted = customer.WaitFor<DeliveryStatusNotify>()
            .Where(message => message.Payload.DeliveryId == deliveryId && message.Payload.Status == DeliveryStatus.Accepted)
            .Async(cancellationToken);
        var pickedUp = customer.WaitFor<DeliveryStatusNotify>()
            .Where(message => message.Payload.DeliveryId == deliveryId && message.Payload.Status == DeliveryStatus.PickedUp)
            .Async(cancellationToken);
        var delivered = customer.WaitFor<DeliveryStatusNotify>()
            .Where(message => message.Payload.DeliveryId == deliveryId && message.Payload.Status == DeliveryStatus.Delivered)
            .Async(cancellationToken);

        var subscribed = await customer.Request(new SubscribeDelivery(deliveryId))
            .Async<SubscribeDeliveryAccepted>(cancellationToken);
        Ensure(subscribed.DeliveryId == deliveryId);

        var created = http.Post("/deliveries")
            .Body(new CreateDeliveryRequest(
                deliveryId,
                "customer-1",
                "Kitchen 12",
                "Customer Lobby"))
            .Fetch<DeliveryCreated>();
        Ensure(created.DeliveryId == deliveryId);

        var courierOffer = (await offer).Payload;
        await courier.Send(new CourierDecision(
                courierOffer.DeliveryId,
                courierOffer.CourierId,
                Accepted: true,
                Reason: null))
            .Async(cancellationToken);

        Ensure((await assigned).Payload.CourierId == "courier-a");
        Ensure((await accepted).Payload.CourierId == "courier-a");
        Ensure((await pickedUp).Payload.CourierId == "courier-a");
        Ensure((await delivered).Payload.CourierId == "courier-a");
    }

    private static async ValueTask RunReassignedDeliveryAsync(
        ZLinkHttpClient http,
        IZlinkStreamConnector customer,
        IZlinkStreamConnector courier,
        CancellationToken cancellationToken)
    {
        var deliveryId = "delivery-reassign";
        var firstOffer = courier.WaitFor<OfferDelivery>()
            .Where(message => message.Payload.DeliveryId == deliveryId)
            .Where(message => message.Payload.CourierId == "courier-a")
            .Async(cancellationToken);
        var secondOffer = courier.WaitFor<OfferDelivery>()
            .Where(message => message.Payload.DeliveryId == deliveryId)
            .Where(message => message.Payload.CourierId == "courier-b")
            .Async(cancellationToken);
        var assigned = customer.WaitFor<DeliveryStatusNotify>()
            .Where(message => message.Payload.DeliveryId == deliveryId && message.Payload.Status == DeliveryStatus.Assigned)
            .Async(cancellationToken);
        var reassigned = customer.WaitFor<DeliveryStatusNotify>()
            .Where(message => message.Payload.DeliveryId == deliveryId && message.Payload.Status == DeliveryStatus.Reassigned)
            .Async(cancellationToken);
        var accepted = customer.WaitFor<DeliveryStatusNotify>()
            .Where(message => message.Payload.DeliveryId == deliveryId && message.Payload.Status == DeliveryStatus.Accepted)
            .Async(cancellationToken);
        var delivered = customer.WaitFor<DeliveryStatusNotify>()
            .Where(message => message.Payload.DeliveryId == deliveryId && message.Payload.Status == DeliveryStatus.Delivered)
            .Async(cancellationToken);

        var subscribed = await customer.Request(new SubscribeDelivery(deliveryId))
            .Async<SubscribeDeliveryAccepted>(cancellationToken);
        Ensure(subscribed.DeliveryId == deliveryId);

        var created = http.Post("/deliveries")
            .Body(new CreateDeliveryRequest(
                deliveryId,
                "customer-1",
                "Kitchen 12",
                "Customer Lobby"))
            .Fetch<DeliveryCreated>();
        Ensure(created.DeliveryId == deliveryId);

        _ = await firstOffer;
        var acceptedOffer = (await secondOffer).Payload;
        await courier.Send(new CourierDecision(
                acceptedOffer.DeliveryId,
                acceptedOffer.CourierId,
                Accepted: true,
                Reason: null))
            .Async(cancellationToken);

        Ensure((await assigned).Payload.CourierId == "courier-a");
        Ensure((await reassigned).Payload.CourierId == "courier-b");
        Ensure((await accepted).Payload.CourierId == "courier-b");
        Ensure((await delivered).Payload.CourierId == "courier-b");
        Console.WriteLine("deliverydispatch-reassignment=completed");
    }

    private static ValueTask AssertServerEvidenceAsync(
        ZLinkHttpClient http,
        CancellationToken cancellationToken)
    {
        var assertion = http.Post("/self-check/assert")
            .Body(new ServerAssertionReq("delivery-success", "delivery-reassign"))
            .Fetch<ServerAssertionRes>();
        Ensure(assertion.Passed);
        Console.WriteLine("deliverydispatch-server-evidence=completed");
        return ValueTask.CompletedTask;
    }

    private static void Ensure(
        bool condition,
        [CallerArgumentExpression(nameof(condition))] string? expression = null)
    {
        if (!condition)
        {
            throw new InvalidOperationException($"Ensure failed: {expression}");
        }
    }
}
