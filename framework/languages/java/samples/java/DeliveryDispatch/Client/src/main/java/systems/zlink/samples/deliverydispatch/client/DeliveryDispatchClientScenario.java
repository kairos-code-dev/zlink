package systems.zlink.samples.deliverydispatch.client;

import systems.zlink.httpclient.ZLinkHttpClient;
import systems.zlink.samples.deliverydispatch.client.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages.Status;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamMessage;

public final class DeliveryDispatchClientScenario {
    private final ZLinkHttpClient api;

    public DeliveryDispatchClientScenario(ZLinkHttpClient api) {
        this.api = api;
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

        Messages.SubscribeDeliveryReqRes subscribed = customer
            .request(new Messages.SubscribeDeliveryReq(deliveryId))
            .await(Messages.SubscribeDeliveryReqRes.class);
        ensure(subscribed.deliveryId().equals(deliveryId));

        Messages.CreateDeliveryRes created = createDelivery(deliveryId);
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

        Messages.SubscribeDeliveryReqRes subscribed = customer
            .request(new Messages.SubscribeDeliveryReq(deliveryId))
            .await(Messages.SubscribeDeliveryReqRes.class);
        ensure(subscribed.deliveryId().equals(deliveryId));

        Messages.CreateDeliveryRes created = createDelivery(deliveryId);
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
            .submit(Messages.DeliveryStatusNotify.class);
    }

    private Messages.CreateDeliveryRes createDelivery(String deliveryId) {
        return api
            .post("/deliveries")
            .body(new Messages.CreateDeliveryReq(
                    deliveryId,
                    "customer-1",
                    "Kitchen 12",
                    "Customer Lobby"))
            .fetch(Messages.CreateDeliveryRes.class);
    }

    private void assertServerEvidence() {
        Messages.ServerAssertionRes assertion = api
            .post("/self-check/assert")
            .body(new Messages.ServerAssertionReq("delivery-success", "delivery-reassign"))
            .fetch(Messages.ServerAssertionRes.class);
        ensure(assertion.passed());
        System.out.println("deliverydispatch-server-evidence=completed");
    }

    private static void ensure(boolean condition) {
        if (!condition) {
            throw new IllegalStateException("Ensure failed");
        }
    }
}
