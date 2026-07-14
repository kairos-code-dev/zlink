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
        var courierABinding = await courierA.Request(new BindCourierSessionReq("courier-a"))
            .Async<BindCourierSessionRes>(cancellationToken);
        Ensure(courierABinding.CourierId == "courier-a");
        Ensure(courierABinding.Actor.NodeRid == "delivery-courier-node-1");
        var courierBBinding = await courierB.Request(new BindCourierSessionReq("courier-b"))
            .Async<BindCourierSessionRes>(cancellationToken);
        Ensure(courierBBinding.CourierId == "courier-b");
        Ensure(courierBBinding.Actor.NodeRid == "delivery-courier-node-2");
        logger.LogInformation("topology=ready");

        // Run both dispatch paths: direct acceptance first, then timeout-based reassignment.
        await RunSuccessfulDeliveryAsync(http, customer, courierA, cancellationToken);
        await RunReassignedDeliveryAsync(http, customer, courierA, courierB, cancellationToken);
        await AssertServerEvidenceAsync(http, cancellationToken);
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

        var subscribed = await customer.Request(new SubscribeDeliveryReq(deliveryId))
            .Async<SubscribeDeliveryRes>(cancellationToken);
        Ensure(subscribed.DeliveryId == deliveryId);
        var statusSequenceTask = WaitForStatusSequenceAsync(
            customer,
            deliveryId,
            [
                DeliveryStatus.Assigned,
                DeliveryStatus.Accepted,
                DeliveryStatus.PickedUp,
                DeliveryStatus.Delivered
            ],
            cancellationToken).AsTask();

        var created = http.Post("/deliveries")
            .Body(new CreateDeliveryReq(
                deliveryId,
                "customer-1",
                "Kitchen 12",
                "Customer Lobby"))
            .Async<CreateDeliveryRes>().AsTask().GetAwaiter().GetResult().Body;
        Ensure(created.DeliveryId == deliveryId);

        // courier-a receives the offer through its bound stream session and accepts it.
        var courierOffer = (await offer).Payload;
        courier.Send(new CourierDecisionMsg(
                courierOffer.DeliveryId,
                courierOffer.CourierId,
                true,
                null))
            .Submit(cancellationToken);

        var statuses = await statusSequenceTask;
        var assigned = statuses[0];
        var accepted = statuses[1];
        var pickedUp = statuses[2];
        var delivered = statuses[3];

        Ensure(assigned.CourierId == "courier-a");
        Ensure(accepted.CourierId == "courier-a");
        Ensure(pickedUp.CourierId == "courier-a");
        Ensure(delivered.CourierId == "courier-a");
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

        var subscribed = await customer.Request(new SubscribeDeliveryReq(deliveryId))
            .Async<SubscribeDeliveryRes>(cancellationToken);
        Ensure(subscribed.DeliveryId == deliveryId);
        var statusSequenceTask = WaitForStatusSequenceAsync(
            customer,
            deliveryId,
            [
                DeliveryStatus.Assigned,
                DeliveryStatus.Reassigned,
                DeliveryStatus.Accepted,
                DeliveryStatus.Delivered
            ],
            cancellationToken).AsTask();

        var created = http.Post("/deliveries")
            .Body(new CreateDeliveryReq(
                deliveryId,
                "customer-1",
                "Kitchen 12",
                "Customer Lobby"))
            .Async<CreateDeliveryRes>().AsTask().GetAwaiter().GetResult().Body;
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

        var statuses = await statusSequenceTask;
        var assigned = statuses[0];
        var reassigned = statuses[1];
        var accepted = statuses[2];
        var delivered = statuses[3];

        Ensure(assigned.CourierId == "courier-a");
        Ensure(reassigned.CourierId == "courier-b");
        Ensure(accepted.CourierId == "courier-b");
        Ensure(delivered.CourierId == "courier-b");
        logger.LogInformation("deliverydispatch-reassignment=completed");
    }

    private static async ValueTask<IReadOnlyList<DeliveryStatusNotify>> WaitForStatusSequenceAsync(
        IZlinkStreamConnector customer,
        string deliveryId,
        IReadOnlyList<DeliveryStatus> expectedStatuses,
        CancellationToken cancellationToken)
    {
        var statuses = new List<DeliveryStatusNotify>(expectedStatuses.Count);
        foreach (var expectedStatus in expectedStatuses)
        {
            // Filter only by delivery id. Filtering by the expected status would hide an
            // out-of-order notification and turn this release gate back into a presence check.
            var received = await customer.WaitFor<DeliveryStatusNotify>()
                .Where(message => message.Payload.DeliveryId == deliveryId)
                .Async(cancellationToken);
            var message = received.Payload;
            Ensure(message.Status == expectedStatus);
            statuses.Add(message);
        }

        return statuses;
    }

    private ValueTask AssertServerEvidenceAsync(
        ZLinkHttpClient http,
        CancellationToken cancellationToken)
    {
        // The final HTTP check proves that server-side tracking saw the full status sequences.
        var assertion = http.Post("/self-check/assert")
            .Body(new ServerAssertionReq("delivery-success", "delivery-reassign"))
            .Async<ServerAssertionRes>().AsTask().GetAwaiter().GetResult().Body;
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
