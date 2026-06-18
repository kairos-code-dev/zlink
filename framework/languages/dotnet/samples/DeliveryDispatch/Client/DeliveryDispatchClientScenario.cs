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
        CancellationToken cancellationToken = default)
    {
        await customer.Connect.Async(cancellationToken);

        await RunSuccessfulDeliveryAsync(http, customer, cancellationToken);
        await RunReassignedDeliveryAsync(http, customer, cancellationToken);
        await AssertServerEvidenceAsync(http, cancellationToken);
    }

    private static async ValueTask RunSuccessfulDeliveryAsync(
        ZLinkHttpClient http,
        IZlinkStreamConnector customer,
        CancellationToken cancellationToken)
    {
        var deliveryId = "delivery-success";
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

        Ensure((await assigned).Payload.CourierId == "courier-a");
        Ensure((await accepted).Payload.CourierId == "courier-a");
        Ensure((await pickedUp).Payload.CourierId == "courier-a");
        Ensure((await delivered).Payload.CourierId == "courier-a");
    }

    private static async ValueTask RunReassignedDeliveryAsync(
        ZLinkHttpClient http,
        IZlinkStreamConnector customer,
        CancellationToken cancellationToken)
    {
        var deliveryId = "delivery-reassign";
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
