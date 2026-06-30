using System.Runtime.CompilerServices;
using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

namespace DeliveryDispatch.Client;

internal sealed class DeliveryDispatchClientScenario(ILogger logger)
{
    // End-to-end client story:
    // 1. Open one customer stream session and two courier stream sessions.
    // 2. Bind courier-a and courier-b independently so each courier actor can push to its own client.
    // 3. Create a delivery where courier-a receives an offer and accepts it.
    // 4. Create another delivery where courier-a receives an offer but stays silent.
    // 5. Let the dispatch server time out courier-a, reassign to courier-b, and finish the delivery.
    // 6. Ask the server to verify that tracking saw the expected status sequences.
    public async ValueTask RunAsync(
        ZLinkHttpClient http,
        IZlinkStreamConnector customer,
        IZlinkStreamConnector courierA,
        IZlinkStreamConnector courierB,
        CancellationToken cancellationToken = default)
    {
        // The sample uses three logical client sessions:
        // one customer session and one independent courier session per courier.
        await customer.Connect.Async(cancellationToken);
        await courierA.Connect.Async(cancellationToken);
        await courierB.Connect.Async(cancellationToken);

        // Each courier binds its own stream session to the actor chosen by the server side directory.
        _ = await BindCourierAsync(courierA, "courier-a", cancellationToken);
        _ = await BindCourierAsync(courierB, "courier-b", cancellationToken);

        // Run both dispatch paths: direct acceptance first, then timeout-based reassignment.
        await RunSuccessfulDeliveryAsync(http, customer, courierA, cancellationToken);
        await RunReassignedDeliveryAsync(http, customer, courierA, courierB, cancellationToken);
        await AssertServerEvidenceAsync(http, cancellationToken);
    }

    private static async ValueTask<BindCourierSessionRes> BindCourierAsync(
        IZlinkStreamConnector courier,
        string courierId,
        CancellationToken cancellationToken)
    {
        // The client only identifies the courier. The server attaches the current stream session
        // to the courier actor and replies with the actor/session binding evidence.
        return await courier.Request(new BindCourierSessionReq(courierId))
            .Async<BindCourierSessionRes>(cancellationToken);
    }

    private static async ValueTask RunSuccessfulDeliveryAsync(
        ZLinkHttpClient http,
        IZlinkStreamConnector customer,
        IZlinkStreamConnector courier,
        CancellationToken cancellationToken)
    {
        // Success path: customer subscribes, dispatch creates the delivery,
        // courier-a receives the offer through stream push, then accepts.
        var deliveryId = "delivery-success";

        // Register waits before creating the delivery so push messages cannot race past the client.
        var offer = courier.WaitFor<OfferDeliveryNotify>()
            .Where(message => message.Payload.DeliveryId == deliveryId)
            .Async(cancellationToken);
        var assigned = customer.WaitFor<DeliveryStatusNotify>()
            .Where(message =>
                message.Payload.DeliveryId == deliveryId && message.Payload.Status == DeliveryStatus.Assigned)
            .Async(cancellationToken);
        var accepted = customer.WaitFor<DeliveryStatusNotify>()
            .Where(message =>
                message.Payload.DeliveryId == deliveryId && message.Payload.Status == DeliveryStatus.Accepted)
            .Async(cancellationToken);
        var pickedUp = customer.WaitFor<DeliveryStatusNotify>()
            .Where(message =>
                message.Payload.DeliveryId == deliveryId && message.Payload.Status == DeliveryStatus.PickedUp)
            .Async(cancellationToken);
        var delivered = customer.WaitFor<DeliveryStatusNotify>()
            .Where(message =>
                message.Payload.DeliveryId == deliveryId && message.Payload.Status == DeliveryStatus.Delivered)
            .Async(cancellationToken);

        var subscribed = await customer.Request(new SubscribeDeliveryReq(deliveryId))
            .Async<SubscribeDeliveryRes>(cancellationToken);
        Ensure(subscribed.DeliveryId == deliveryId);

        var created = http.Post("/deliveries")
            .Body(new CreateDeliveryReq(
                deliveryId,
                "customer-1",
                "Kitchen 12",
                "Customer Lobby"))
            .Fetch<CreateDeliveryRes>();
        Ensure(created.DeliveryId == deliveryId);

        // courier-a receives the offer through its bound stream session and accepts it.
        var courierOffer = (await offer).Payload;
        courier.Send(new CourierDecisionMsg(
                courierOffer.DeliveryId,
                courierOffer.CourierId,
                true,
                null))
            .Submit(cancellationToken);

        Ensure((await assigned).Payload.CourierId == "courier-a");
        Ensure((await accepted).Payload.CourierId == "courier-a");
        Ensure((await pickedUp).Payload.CourierId == "courier-a");
        Ensure((await delivered).Payload.CourierId == "courier-a");
    }

    private async ValueTask RunReassignedDeliveryAsync(
        ZLinkHttpClient http,
        IZlinkStreamConnector customer,
        IZlinkStreamConnector courierA,
        IZlinkStreamConnector courierB,
        CancellationToken cancellationToken)
    {
        // Reassignment path: courier-a receives the first offer and does not respond.
        // The server timeout causes the same delivery to be offered to courier-b.
        var deliveryId = "delivery-reassign";

        // courier-a and courier-b are different stream sessions, so each waits on its own connector.
        var firstOffer = courierA.WaitFor<OfferDeliveryNotify>()
            .Where(message => message.Payload.DeliveryId == deliveryId)
            .Where(message => message.Payload.CourierId == "courier-a")
            .Async(cancellationToken);
        var secondOffer = courierB.WaitFor<OfferDeliveryNotify>()
            .Where(message => message.Payload.DeliveryId == deliveryId)
            .Where(message => message.Payload.CourierId == "courier-b")
            .Async(cancellationToken);
        var assigned = customer.WaitFor<DeliveryStatusNotify>()
            .Where(message =>
                message.Payload.DeliveryId == deliveryId && message.Payload.Status == DeliveryStatus.Assigned)
            .Async(cancellationToken);
        var reassigned = customer.WaitFor<DeliveryStatusNotify>()
            .Where(message =>
                message.Payload.DeliveryId == deliveryId && message.Payload.Status == DeliveryStatus.Reassigned)
            .Async(cancellationToken);
        var accepted = customer.WaitFor<DeliveryStatusNotify>()
            .Where(message =>
                message.Payload.DeliveryId == deliveryId && message.Payload.Status == DeliveryStatus.Accepted)
            .Async(cancellationToken);
        var delivered = customer.WaitFor<DeliveryStatusNotify>()
            .Where(message =>
                message.Payload.DeliveryId == deliveryId && message.Payload.Status == DeliveryStatus.Delivered)
            .Async(cancellationToken);

        var subscribed = await customer.Request(new SubscribeDeliveryReq(deliveryId))
            .Async<SubscribeDeliveryRes>(cancellationToken);
        Ensure(subscribed.DeliveryId == deliveryId);

        var created = http.Post("/deliveries")
            .Body(new CreateDeliveryReq(
                deliveryId,
                "customer-1",
                "Kitchen 12",
                "Customer Lobby"))
            .Fetch<CreateDeliveryRes>();
        Ensure(created.DeliveryId == deliveryId);

        // courier-a intentionally does not answer. The dispatch server times out and offers the
        // same delivery to courier-b, which accepts through its own bound stream session.
        _ = await firstOffer;
        var acceptedOffer = (await secondOffer).Payload;
        courierB.Send(new CourierDecisionMsg(
                acceptedOffer.DeliveryId,
                acceptedOffer.CourierId,
                true,
                null))
            .Submit(cancellationToken);

        Ensure((await assigned).Payload.CourierId == "courier-a");
        Ensure((await reassigned).Payload.CourierId == "courier-b");
        Ensure((await accepted).Payload.CourierId == "courier-b");
        Ensure((await delivered).Payload.CourierId == "courier-b");
        logger.LogInformation("deliverydispatch-reassignment=completed");
    }

    private ValueTask AssertServerEvidenceAsync(
        ZLinkHttpClient http,
        CancellationToken cancellationToken)
    {
        // The final HTTP check proves that server-side tracking saw the full status sequences.
        var assertion = http.Post("/self-check/assert")
            .Body(new ServerAssertionReq("delivery-success", "delivery-reassign"))
            .Fetch<ServerAssertionRes>();
        Ensure(assertion.Passed);
        logger.LogInformation("deliverydispatch-server-evidence=completed");
        return ValueTask.CompletedTask;
    }

    private static void Ensure(
        bool condition,
        [CallerArgumentExpression(nameof(condition))]
        string? expression = null)
    {
        if (!condition) throw new InvalidOperationException($"Ensure failed: {expression}");
    }
}
