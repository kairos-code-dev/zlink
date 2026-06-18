using System.Net.Http.Json;
using System.Runtime.CompilerServices;
using DeliveryDispatch.Shared.Contracts;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Json;

namespace DeliveryDispatch.Client;

internal sealed class DeliveryDispatchClientScenario
{
    public async ValueTask RunAsync(
        HttpClient http,
        IZlinkStreamConnector customer,
        CancellationToken cancellationToken = default)
    {
        await customer.Connect.Async(cancellationToken);

        await RunSuccessfulDeliveryAsync(http, customer, cancellationToken);
        await RunReassignedDeliveryAsync(http, customer, cancellationToken);
        await AssertServerEvidenceAsync(http, cancellationToken);
    }

    private static async ValueTask RunSuccessfulDeliveryAsync(
        HttpClient http,
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

        var created = await CreateDeliveryAsync(http, deliveryId, cancellationToken);
        Ensure(created.DeliveryId == deliveryId);

        Ensure((await assigned).Payload.CourierId == "courier-a");
        Ensure((await accepted).Payload.CourierId == "courier-a");
        Ensure((await pickedUp).Payload.CourierId == "courier-a");
        Ensure((await delivered).Payload.CourierId == "courier-a");
    }

    private static async ValueTask RunReassignedDeliveryAsync(
        HttpClient http,
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

        var created = await CreateDeliveryAsync(http, deliveryId, cancellationToken);
        Ensure(created.DeliveryId == deliveryId);

        Ensure((await assigned).Payload.CourierId == "courier-a");
        Ensure((await reassigned).Payload.CourierId == "courier-b");
        Ensure((await accepted).Payload.CourierId == "courier-b");
        Ensure((await delivered).Payload.CourierId == "courier-b");
        Console.WriteLine("deliverydispatch-reassignment=completed");
    }

    private static async ValueTask<DeliveryCreated> CreateDeliveryAsync(
        HttpClient http,
        string deliveryId,
        CancellationToken cancellationToken)
    {
        var response = await http.PostAsJsonAsync(
            "/deliveries",
            new CreateDeliveryRequest(
                deliveryId,
                "customer-1",
                "Kitchen 12",
                "Customer Lobby"),
            cancellationToken);
        response.EnsureSuccessStatusCode();
        return await response.Content.ReadFromJsonAsync<DeliveryCreated>(cancellationToken)
            ?? throw new InvalidOperationException("Delivery API returned an empty response.");
    }

    private static async ValueTask AssertServerEvidenceAsync(
        HttpClient http,
        CancellationToken cancellationToken)
    {
        var response = await http.PostAsJsonAsync(
            "/self-check/assert",
            new ServerAssertionReq("delivery-success", "delivery-reassign"),
            cancellationToken);
        response.EnsureSuccessStatusCode();
        var assertion = await response.Content.ReadFromJsonAsync<ServerAssertionRes>(cancellationToken)
            ?? throw new InvalidOperationException("Server assertion returned an empty response.");
        Ensure(assertion.Passed);
        Console.WriteLine("deliverydispatch-server-evidence=completed");
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
