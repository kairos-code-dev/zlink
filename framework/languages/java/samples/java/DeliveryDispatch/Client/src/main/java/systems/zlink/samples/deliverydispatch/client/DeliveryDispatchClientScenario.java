package systems.zlink.samples.deliverydispatch.client;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.util.concurrent.CompletionStage;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTopology;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamMessage;

public final class DeliveryDispatchClientScenario {
    private final ObjectMapper json = new ObjectMapper();
    private final HttpClient http = HttpClient.newHttpClient();

    public void run(
        ZLinkStreamConnector customer,
        ZLinkStreamConnector courierA,
        ZLinkStreamConnector courierB) throws Exception {
        customer.connect().await();
        courierA.connect().await();
        courierB.connect().await();

        Messages.BindCourierSessionAccepted courierABound =
            courierA.request(new Messages.BindCourierSession("courier-a"))
                .await(Messages.BindCourierSessionAccepted.class);
        ensure(courierABound.courierId().equals("courier-a"));
        Messages.BindCourierSessionAccepted courierBBound =
            courierB.request(new Messages.BindCourierSession("courier-b"))
                .await(Messages.BindCourierSessionAccepted.class);
        ensure(courierBBound.courierId().equals("courier-b"));
        ensure(!courierABound.actor().nodeRid().equals(courierBBound.actor().nodeRid()));

        runSuccessfulDelivery(customer, courierA);
        runReassignedDelivery(customer, courierA, courierB);
        assertServerEvidence();
    }

    private void runSuccessfulDelivery(
        ZLinkStreamConnector customer,
        ZLinkStreamConnector courier) throws Exception {
        String deliveryId = "delivery-success";
        CompletionStage<ZLinkStreamMessage<Messages.OfferDeliveryNotify>> offer =
            courier.waitFor(Messages.OfferDeliveryNotify.class)
                .where(Messages.OfferDeliveryNotify.class, message -> message.payload().deliveryId().equals(deliveryId))
                .submit(Messages.OfferDeliveryNotify.class);
        CompletionStage<ZLinkStreamMessage<Messages.DeliveryStatusNotify>> assigned =
            waitStatus(customer, deliveryId, Messages.DeliveryStatus.Assigned);
        CompletionStage<ZLinkStreamMessage<Messages.DeliveryStatusNotify>> accepted =
            waitStatus(customer, deliveryId, Messages.DeliveryStatus.Accepted);
        CompletionStage<ZLinkStreamMessage<Messages.DeliveryStatusNotify>> pickedUp =
            waitStatus(customer, deliveryId, Messages.DeliveryStatus.PickedUp);
        CompletionStage<ZLinkStreamMessage<Messages.DeliveryStatusNotify>> delivered =
            waitStatus(customer, deliveryId, Messages.DeliveryStatus.Delivered);

        Messages.SubscribeDeliveryAccepted subscribed =
            customer.request(new Messages.SubscribeDelivery(deliveryId))
                .await(Messages.SubscribeDeliveryAccepted.class);
        ensure(subscribed.deliveryId().equals(deliveryId));

        Messages.CreateDeliveryResponse created = post(
            "/deliveries",
            new Messages.CreateDeliveryRequest(
                deliveryId,
                "customer-1",
                "Kitchen 12",
                "Customer Lobby"),
            Messages.CreateDeliveryResponse.class);
        ensure(created.deliveryId().equals(deliveryId));

        Messages.OfferDeliveryNotify courierOffer = courier.await(offer).payload();
        courier.send(new Messages.CourierDecision(
                courierOffer.deliveryId(),
                courierOffer.courierId(),
                true,
                null))
            .submit();

        ensure(customer.await(assigned).payload().courierId().equals("courier-a"));
        ensure(customer.await(accepted).payload().courierId().equals("courier-a"));
        ensure(customer.await(pickedUp).payload().courierId().equals("courier-a"));
        ensure(customer.await(delivered).payload().courierId().equals("courier-a"));
    }

    private void runReassignedDelivery(
        ZLinkStreamConnector customer,
        ZLinkStreamConnector courierA,
        ZLinkStreamConnector courierB) throws Exception {
        String deliveryId = "delivery-reassign";
        CompletionStage<ZLinkStreamMessage<Messages.OfferDeliveryNotify>> firstOffer =
            courierA.waitFor(Messages.OfferDeliveryNotify.class)
                .where(Messages.OfferDeliveryNotify.class, message ->
                    message.payload().deliveryId().equals(deliveryId)
                        && message.payload().courierId().equals("courier-a"))
                .submit(Messages.OfferDeliveryNotify.class);
        CompletionStage<ZLinkStreamMessage<Messages.OfferDeliveryNotify>> secondOffer =
            courierB.waitFor(Messages.OfferDeliveryNotify.class)
                .where(Messages.OfferDeliveryNotify.class, message ->
                    message.payload().deliveryId().equals(deliveryId)
                        && message.payload().courierId().equals("courier-b"))
                .submit(Messages.OfferDeliveryNotify.class);
        CompletionStage<ZLinkStreamMessage<Messages.DeliveryStatusNotify>> assigned =
            waitStatus(customer, deliveryId, Messages.DeliveryStatus.Assigned);
        CompletionStage<ZLinkStreamMessage<Messages.DeliveryStatusNotify>> reassigned =
            waitStatus(customer, deliveryId, Messages.DeliveryStatus.Reassigned);
        CompletionStage<ZLinkStreamMessage<Messages.DeliveryStatusNotify>> accepted =
            waitStatus(customer, deliveryId, Messages.DeliveryStatus.Accepted);
        CompletionStage<ZLinkStreamMessage<Messages.DeliveryStatusNotify>> delivered =
            waitStatus(customer, deliveryId, Messages.DeliveryStatus.Delivered);

        Messages.SubscribeDeliveryAccepted subscribed =
            customer.request(new Messages.SubscribeDelivery(deliveryId))
                .await(Messages.SubscribeDeliveryAccepted.class);
        ensure(subscribed.deliveryId().equals(deliveryId));

        Messages.CreateDeliveryResponse created = post(
            "/deliveries",
            new Messages.CreateDeliveryRequest(
                deliveryId,
                "customer-1",
                "Kitchen 12",
                "Customer Lobby"),
            Messages.CreateDeliveryResponse.class);
        ensure(created.deliveryId().equals(deliveryId));

        courierA.await(firstOffer);
        Messages.OfferDeliveryNotify acceptedOffer = courierB.await(secondOffer).payload();
        courierB.send(new Messages.CourierDecision(
                acceptedOffer.deliveryId(),
                acceptedOffer.courierId(),
                true,
                null))
            .submit();

        ensure(customer.await(assigned).payload().courierId().equals("courier-a"));
        ensure(customer.await(reassigned).payload().courierId().equals("courier-b"));
        ensure(customer.await(accepted).payload().courierId().equals("courier-b"));
        ensure(customer.await(delivered).payload().courierId().equals("courier-b"));
        System.out.println(SampleNames.ReassignmentMarker);
    }

    private CompletionStage<ZLinkStreamMessage<Messages.DeliveryStatusNotify>> waitStatus(
        ZLinkStreamConnector customer,
        String deliveryId,
        Messages.DeliveryStatus status) {
        return customer.waitFor(Messages.DeliveryStatusNotify.class)
            .where(Messages.DeliveryStatusNotify.class, message ->
                message.payload().deliveryId().equals(deliveryId)
                    && message.payload().status() == status)
            .submit(Messages.DeliveryStatusNotify.class);
    }

    private void assertServerEvidence() throws Exception {
        Messages.ServerAssertionResponse response = post(
            "/self-check/assert",
            new Messages.ServerAssertionRequest("delivery-success", "delivery-reassign"),
            Messages.ServerAssertionResponse.class);
        ensure(response.passed());
        System.out.println(SampleNames.ServerEvidenceMarker);
    }

    private <TResponse> TResponse post(
        String path,
        Object body,
        Class<TResponse> responseType) throws Exception {
        HttpRequest request = HttpRequest.newBuilder()
            .uri(URI.create(SampleTopology.DispatchHttpEndpoint + path))
            .header("content-type", "application/json")
            .POST(HttpRequest.BodyPublishers.ofString(json.writeValueAsString(body)))
            .build();
        HttpResponse<String> response = http.send(request, HttpResponse.BodyHandlers.ofString());
        if (response.statusCode() < 200 || response.statusCode() >= 300) {
            throw new IllegalStateException("HTTP " + response.statusCode() + " for " + path + ": "
                + response.body());
        }
        return json.readValue(response.body(), responseType);
    }

    private static void ensure(boolean condition) {
        if (!condition) {
            throw new IllegalStateException("Ensure failed");
        }
    }
}
