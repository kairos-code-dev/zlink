package systems.zlink.samples.deliverydispatch.client;

import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.samples.deliverydispatch.client.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.client.configuration.SampleTimings;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages.Status;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamMessage;

public final class DeliveryDispatchClientScenario {
    private final ZLinkClient channels;

    public DeliveryDispatchClientScenario(ZLinkClient channels) {
        this.channels = channels;
    }

    public void run(ZLinkStreamConnector customer) throws Exception {
        customer.connect().await();
        runSuccessfulDelivery(customer);
        runReassignedDelivery(customer);
        assertServerEvidence();
    }

    private void runSuccessfulDelivery(ZLinkStreamConnector customer) throws Exception {
        String deliveryId = "delivery-success";
        var assigned = awaitStatus(customer, deliveryId, Status.Assigned);
        var accepted = awaitStatus(customer, deliveryId, Status.Accepted);
        var pickedUp = awaitStatus(customer, deliveryId, Status.PickedUp);
        var delivered = awaitStatus(customer, deliveryId, Status.Delivered);

        Messages.SubscribeDeliveryAccepted subscribed = customer
            .request(new Messages.SubscribeDelivery(deliveryId))
            .await(Messages.SubscribeDeliveryAccepted.class);
        ensure(subscribed.deliveryId().equals(deliveryId));

        Messages.DeliveryCreated created = createDelivery(deliveryId);
        ensure(created.deliveryId().equals(deliveryId));

        ensure(customer.await(assigned).payload().courierId().equals(SampleNames.CourierA));
        ensure(customer.await(accepted).payload().courierId().equals(SampleNames.CourierA));
        ensure(customer.await(pickedUp).payload().courierId().equals(SampleNames.CourierA));
        ensure(customer.await(delivered).payload().courierId().equals(SampleNames.CourierA));
    }

    private void runReassignedDelivery(ZLinkStreamConnector customer) throws Exception {
        String deliveryId = "delivery-reassign";
        var assigned = awaitStatus(customer, deliveryId, Status.Assigned);
        var reassigned = awaitStatus(customer, deliveryId, Status.Reassigned);
        var accepted = awaitStatus(customer, deliveryId, Status.Accepted);
        var delivered = awaitStatus(customer, deliveryId, Status.Delivered);

        Messages.SubscribeDeliveryAccepted subscribed = customer
            .request(new Messages.SubscribeDelivery(deliveryId))
            .await(Messages.SubscribeDeliveryAccepted.class);
        ensure(subscribed.deliveryId().equals(deliveryId));

        Messages.DeliveryCreated created = createDelivery(deliveryId);
        ensure(created.deliveryId().equals(deliveryId));

        ensure(customer.await(assigned).payload().courierId().equals(SampleNames.CourierA));
        ensure(customer.await(reassigned).payload().courierId().equals(SampleNames.CourierB));
        ensure(customer.await(accepted).payload().courierId().equals(SampleNames.CourierB));
        ensure(customer.await(delivered).payload().courierId().equals(SampleNames.CourierB));
        System.out.println("deliverydispatch-reassignment=completed");
    }

    private java.util.concurrent.CompletionStage<ZLinkStreamMessage<Messages.DeliveryStatusNotify>> awaitStatus(
        ZLinkStreamConnector customer,
        String deliveryId,
        String status) {
        return customer.waitFor(Messages.DeliveryStatusNotify.class)
            .where(
                Messages.DeliveryStatusNotify.class,
                message -> message.payload().deliveryId().equals(deliveryId)
                    && message.payload().status().equals(status))
            .timeout(SampleTimings.NotifyTimeout)
            .submit(Messages.DeliveryStatusNotify.class);
    }

    private Messages.DeliveryCreated createDelivery(String deliveryId) {
        return channels
            .requestToChannel(
                SampleNames.ApiChannel,
                new Messages.CreateDeliveryRequest(
                    deliveryId,
                    "customer-1",
                    "Kitchen 12",
                    "Customer Lobby"))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.DeliveryCreated.class);
    }

    private void assertServerEvidence() {
        Messages.ServerAssertionRes assertion = channels
            .requestToChannel(
                SampleNames.ApiChannel,
                new Messages.ServerAssertionReq("delivery-success", "delivery-reassign"))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.ServerAssertionRes.class);
        ensure(assertion.passed());
        System.out.println("deliverydispatch-server-evidence=completed");
    }

    private static void ensure(boolean condition) {
        if (!condition) {
            throw new IllegalStateException("Ensure failed");
        }
    }
}
