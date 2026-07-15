package systems.zlink.samples.deliverydispatch.client;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.util.List;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CopyOnWriteArrayList;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTopology;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamMessage;

public final class DeliveryDispatchClientScenario {
    private final ObjectMapper json = new ObjectMapper();
    private final HttpClient http = HttpClient.newHttpClient();

    public CompletionStage<Void> run(
        ZLinkStreamConnector customer,
        ZLinkStreamConnector courierA,
        ZLinkStreamConnector courierB) {
        List<Messages.DeliveryStatusNotify> observedStatuses = new CopyOnWriteArrayList<>();
        AutoCloseable statusObserver = customer.on(
            Messages.DeliveryStatusNotify.class,
            message -> {
                observedStatuses.add(message.payload());
                return CompletableFuture.completedFuture(null);
            });
        return CompletableFuture.allOf(
                customer.connect().submit().toCompletableFuture(),
                courierA.connect().submit().toCompletableFuture(),
                courierB.connect().submit().toCompletableFuture())
            .thenCompose(ignored -> courierA.request(new Messages.BindCourierSessionReq("courier-a"))
                .submit(Messages.BindCourierSessionRes.class))
            .thenCompose(courierABound -> {
                ensure(courierABound.courierId().equals("courier-a"));
                return courierB.request(new Messages.BindCourierSessionReq("courier-b"))
                    .submit(Messages.BindCourierSessionRes.class)
                    .thenApply(courierBBound -> {
                        ensure(courierBBound.courierId().equals("courier-b"));
                        ensure(!courierABound.actor().nodeRid().equals(
                            courierBBound.actor().nodeRid()));
                        return null;
                    });
            })
            .thenCompose(ignored -> runSuccessfulDelivery(customer, courierA, observedStatuses))
            .thenCompose(ignored -> runReassignedDelivery(customer, courierA, courierB, observedStatuses))
            .thenCompose(ignored -> assertServerEvidence())
            .whenComplete((ignored, error) -> close(statusObserver));
    }

    private CompletionStage<Void> runSuccessfulDelivery(
        ZLinkStreamConnector customer,
        ZLinkStreamConnector courier,
        List<Messages.DeliveryStatusNotify> observedStatuses) {
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

        return customer.request(new Messages.SubscribeDeliveryReq(deliveryId))
            .submit(Messages.SubscribeDeliveryRes.class)
            .thenCompose(subscribed -> {
                ensure(subscribed.deliveryId().equals(deliveryId));
                return post("/deliveries", new Messages.CreateDeliveryReq(
                    deliveryId, "customer-1", "Kitchen 12", "Customer Lobby"),
                    Messages.CreateDeliveryRes.class);
            })
            .thenCompose(created -> {
                ensure(created.deliveryId().equals(deliveryId));
                return offer;
            })
            .thenCompose(message -> {
                Messages.OfferDeliveryNotify courierOffer = message.payload();
                courier.send(new Messages.CourierDecision(
                    courierOffer.deliveryId(), courierOffer.courierId(), true, null)).submit();
                return CompletableFuture.allOf(
                    assigned.toCompletableFuture(), accepted.toCompletableFuture(),
                    pickedUp.toCompletableFuture(), delivered.toCompletableFuture());
            })
            .thenRun(() -> {
                assertStatusOrder(observedStatuses, deliveryId, List.of(
                    Messages.DeliveryStatus.Assigned,
                    Messages.DeliveryStatus.Accepted,
                    Messages.DeliveryStatus.PickedUp,
                    Messages.DeliveryStatus.Delivered));
                ensure(assigned.toCompletableFuture().getNow(null).payload().courierId().equals("courier-a"));
                ensure(accepted.toCompletableFuture().getNow(null).payload().courierId().equals("courier-a"));
                ensure(pickedUp.toCompletableFuture().getNow(null).payload().courierId().equals("courier-a"));
                ensure(delivered.toCompletableFuture().getNow(null).payload().courierId().equals("courier-a"));
            });
    }

    private CompletionStage<Void> runReassignedDelivery(
        ZLinkStreamConnector customer,
        ZLinkStreamConnector courierA,
        ZLinkStreamConnector courierB,
        List<Messages.DeliveryStatusNotify> observedStatuses) {
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

        return customer.request(new Messages.SubscribeDeliveryReq(deliveryId))
            .submit(Messages.SubscribeDeliveryRes.class)
            .thenCompose(subscribed -> {
                ensure(subscribed.deliveryId().equals(deliveryId));
                return post("/deliveries", new Messages.CreateDeliveryReq(
                    deliveryId, "customer-1", "Kitchen 12", "Customer Lobby"),
                    Messages.CreateDeliveryRes.class);
            })
            .thenCompose(created -> {
                ensure(created.deliveryId().equals(deliveryId));
                return firstOffer;
            })
            .thenCompose(ignored -> secondOffer)
            .thenCompose(message -> {
                Messages.OfferDeliveryNotify acceptedOffer = message.payload();
                courierB.send(new Messages.CourierDecision(
                    acceptedOffer.deliveryId(), acceptedOffer.courierId(), true, null)).submit();
                return CompletableFuture.allOf(
                    assigned.toCompletableFuture(), reassigned.toCompletableFuture(),
                    accepted.toCompletableFuture(), delivered.toCompletableFuture());
            })
            .thenRun(() -> {
                assertStatusOrder(observedStatuses, deliveryId, List.of(
                    Messages.DeliveryStatus.Assigned,
                    Messages.DeliveryStatus.Reassigned,
                    Messages.DeliveryStatus.Accepted,
                    Messages.DeliveryStatus.Delivered));
                ensure(assigned.toCompletableFuture().getNow(null).payload().courierId().equals("courier-a"));
                ensure(reassigned.toCompletableFuture().getNow(null).payload().courierId().equals("courier-b"));
                ensure(accepted.toCompletableFuture().getNow(null).payload().courierId().equals("courier-b"));
                ensure(delivered.toCompletableFuture().getNow(null).payload().courierId().equals("courier-b"));
                System.out.println(SampleNames.ReassignmentMarker);
            });
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

    private static void assertStatusOrder(
        List<Messages.DeliveryStatusNotify> observedStatuses,
        String deliveryId,
        List<Messages.DeliveryStatus> expected) {
        List<Messages.DeliveryStatus> actual = observedStatuses.stream()
            .filter(notification -> notification.deliveryId().equals(deliveryId))
            .map(Messages.DeliveryStatusNotify::status)
            .filter(expected::contains)
            .toList();
        ensure(actual.equals(expected));
    }

    private static void close(AutoCloseable closeable) {
        try {
            closeable.close();
        } catch (Exception error) {
            throw new IllegalStateException("Failed to close status observer", error);
        }
    }

    private CompletionStage<Void> assertServerEvidence() {
        return post(
            "/self-check/assert",
            new Messages.ServerAssertionRequest("delivery-success", "delivery-reassign"),
            Messages.ServerAssertionResponse.class).thenAccept(response -> {
                ensure(response.passed());
                System.out.println(SampleNames.ServerEvidenceMarker);
            });
    }

    private <TResponse> CompletionStage<TResponse> post(
        String path,
        Object body,
        Class<TResponse> responseType) {
        try {
            HttpRequest request = HttpRequest.newBuilder()
                .uri(URI.create(SampleTopology.DispatchHttpEndpoint + path))
                .header("content-type", "application/json")
                .POST(HttpRequest.BodyPublishers.ofString(json.writeValueAsString(body)))
                .build();
            return http.sendAsync(request, HttpResponse.BodyHandlers.ofString())
                .thenApply(response -> {
                    if (response.statusCode() < 200 || response.statusCode() >= 300) {
                        throw new IllegalStateException("HTTP " + response.statusCode() + " for "
                            + path + ": " + response.body());
                    }
                    try {
                        return json.readValue(response.body(), responseType);
                    } catch (java.io.IOException error) {
                        throw new java.io.UncheckedIOException(error);
                    }
                });
        } catch (java.io.IOException error) {
            return CompletableFuture.failedFuture(error);
        }
    }

    private static void ensure(boolean condition) {
        if (!condition) {
            throw new IllegalStateException("Ensure failed");
        }
    }
}
