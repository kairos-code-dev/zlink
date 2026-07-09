package systems.zlink.samples.shoppingmall.client;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Instant;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.locks.LockSupport;
import systems.zlink.samples.shoppingmall.client.configuration.SampleTimings;
import systems.zlink.samples.shoppingmall.client.configuration.SampleTopology;
import systems.zlink.samples.shoppingmall.server.configuration.SampleNames;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages;

public final class Program {
    private Program() {
    }

    public static void main(String[] args) throws Exception {
        new ShoppingMallClientScenario().run();
        System.out.println(SampleNames.CompletedMarker);
    }

    private static final class ShoppingMallClientScenario {
        private final ObjectMapper json = new ObjectMapper().findAndRegisterModules();
        private final HttpClient http = HttpClient.newHttpClient();

        void run() throws Exception {
            Messages.StartOrderRes success = start(
                SampleTopology.ApiAHttpUrl,
                "cart-success",
                "pm-ok",
                "start-success");
            ensure(Messages.OrderStatuses.Created.equals(success.status()));
            Messages.OrderState confirmed = waitForStatus(success.orderId(), Messages.OrderStatuses.Confirmed);

            Messages.StartOrderRes duplicate = start(
                SampleTopology.ApiBHttpUrl,
                "cart-success",
                "pm-ok",
                "start-success");
            ensure(duplicate.orderId().equals(success.orderId()));

            Messages.StartOrderRes pendingRecovered = post(
                SampleTopology.ApiAHttpUrl,
                "/self-check/idempotency/pending",
                new Messages.StartOrderReq("cart-success", "addr-home", "pm-ok", "pending-recovered"),
                Messages.StartOrderRes.class);
            waitForStatus(pendingRecovered.orderId(), Messages.OrderStatuses.Confirmed);

            CompletableFuture<Messages.StartOrderRes> firstConcurrent = CompletableFuture.supplyAsync(() ->
                uncheckedStart(SampleTopology.ApiAHttpUrl, "cart-success", "pm-ok", "concurrent-order"));
            CompletableFuture<Messages.StartOrderRes> secondConcurrent = CompletableFuture.supplyAsync(() ->
                uncheckedStart(SampleTopology.ApiBHttpUrl, "cart-success", "pm-ok", "concurrent-order"));
            Messages.StartOrderRes concurrentA = firstConcurrent.get();
            Messages.StartOrderRes concurrentB = secondConcurrent.get();
            ensure(concurrentA.orderId().equals(concurrentB.orderId()));
            waitForStatus(concurrentA.orderId(), Messages.OrderStatuses.Confirmed);

            Messages.ContinueOrderWorkflowRes reserved = post(
                SampleTopology.ApiAHttpUrl,
                "/self-check/workflow/inventory-reserved",
                new Messages.StartOrderReq("cart-success", "addr-office", "pm-ok", "reserved-resume"),
                Messages.ContinueOrderWorkflowRes.class);
            ensure(Messages.OrderStatuses.InventoryReserved.equals(reserved.state().status()));
            Messages.ContinueOrderWorkflowRes resumed = post(
                SampleTopology.ApiBHttpUrl,
                "/self-check/workflow/" + reserved.state().orderId() + "/continue",
                "",
                Messages.ContinueOrderWorkflowRes.class);
            ensure(Messages.OrderStatuses.Confirmed.equals(resumed.state().status()));

            Messages.StartOrderRes inventoryFailure = start(
                SampleTopology.ApiAHttpUrl,
                "cart-inventory-fail",
                "pm-ok",
                "inventory-failure");
            waitForStatus(inventoryFailure.orderId(), Messages.OrderStatuses.Failed);

            Messages.StartOrderRes paymentFailure = start(
                SampleTopology.ApiBHttpUrl,
                "cart-payment-fail",
                "pm-decline",
                "payment-failure");
            waitForStatus(paymentFailure.orderId(), Messages.OrderStatuses.Failed);

            ensure(postRaw(
                SampleTopology.ApiAHttpUrl,
                "/self-check/projection/" + confirmed.orderId() + "/delete"));
            Messages.GetOrderStateRes rebuiltByRead = get(SampleTopology.ApiBHttpUrl, "/orders/" + confirmed.orderId(),
                Messages.GetOrderStateRes.class);
            ensure(Messages.OrderStatuses.Confirmed.equals(rebuiltByRead.state().status()));
            Messages.RebuildOrderProjectionRes explicitRebuild = post(
                SampleTopology.ApiAHttpUrl,
                "/self-check/projection/" + confirmed.orderId() + "/rebuild",
                "",
                Messages.RebuildOrderProjectionRes.class);
            ensure(Messages.OrderStatuses.Confirmed.equals(explicitRebuild.state().status()));

            Messages.StartOrderRes scaleOut = start(
                SampleTopology.ApiBHttpUrl,
                "cart-success",
                "pm-ok",
                "scale-out-order");
            waitForStatus(scaleOut.orderId(), Messages.OrderStatuses.Confirmed);

            Messages.ServerAssertionRes assertion = waitForServerAssertion(new Messages.ServerAssertionReq(
                success.orderId(),
                pendingRecovered.orderId(),
                concurrentA.orderId(),
                reserved.state().orderId(),
                inventoryFailure.orderId(),
                paymentFailure.orderId(),
                scaleOut.orderId()));
            ensure(assertion.passed());
            System.out.println(SampleNames.ServerEvidenceMarker);
        }

        private Messages.StartOrderRes uncheckedStart(
            String base,
            String cartId,
            String paymentMethodId,
            String idempotencyKey) {
            try {
                return start(base, cartId, paymentMethodId, idempotencyKey);
            } catch (Exception ex) {
                throw new IllegalStateException(ex);
            }
        }

        private Messages.StartOrderRes start(
            String base,
            String cartId,
            String paymentMethodId,
            String idempotencyKey) throws Exception {
            return post(
                base,
                "/orders/start",
                new Messages.StartOrderReq(cartId, "addr-home", paymentMethodId, idempotencyKey),
                Messages.StartOrderRes.class);
        }

        private Messages.OrderState waitForStatus(String orderId, String status) throws Exception {
            Instant deadline = Instant.now().plus(SampleTimings.WorkflowTimeout);
            Messages.OrderState last = null;
            while (Instant.now().isBefore(deadline)) {
                Messages.GetOrderStateRes response = get(
                    SampleTopology.ApiAHttpUrl,
                    "/orders/" + orderId,
                    Messages.GetOrderStateRes.class);
                last = response.state();
                if (last != null && status.equals(last.status())) {
                    return last;
                }
                LockSupport.parkNanos(100_000_000L);
            }
            throw new IllegalStateException("Timed out waiting for " + orderId + " to reach " + status
                + ", last=" + last);
        }

        private Messages.ServerAssertionRes waitForServerAssertion(Messages.ServerAssertionReq request)
            throws Exception {
            Instant deadline = Instant.now().plus(SampleTimings.WorkflowTimeout);
            Messages.ServerAssertionRes last = null;
            while (Instant.now().isBefore(deadline)) {
                last = post(
                    SampleTopology.ApiAHttpUrl,
                    "/self-check/assert",
                    request,
                    Messages.ServerAssertionRes.class);
                if (last.passed()) {
                    return last;
                }
                LockSupport.parkNanos(100_000_000L);
            }
            return last;
        }

        private boolean postRaw(String base, String path) throws Exception {
            HttpRequest request = HttpRequest.newBuilder()
                .uri(URI.create(base + path))
                .POST(HttpRequest.BodyPublishers.ofString(""))
                .build();
            HttpResponse<String> response = http.send(request, HttpResponse.BodyHandlers.ofString());
            return response.statusCode() >= 200 && response.statusCode() < 300;
        }

        private <T> T get(String base, String path, Class<T> type) throws Exception {
            HttpRequest request = HttpRequest.newBuilder()
                .uri(URI.create(base + path))
                .GET()
                .build();
            HttpResponse<String> response = http.send(request, HttpResponse.BodyHandlers.ofString());
            return read(path, response, type);
        }

        private <T> T post(String base, String path, Object body, Class<T> type) throws Exception {
            String content = body instanceof String value ? value : json.writeValueAsString(body);
            HttpRequest request = HttpRequest.newBuilder()
                .uri(URI.create(base + path))
                .header("content-type", "application/json")
                .POST(HttpRequest.BodyPublishers.ofString(content))
                .build();
            HttpResponse<String> response = http.send(request, HttpResponse.BodyHandlers.ofString());
            return read(path, response, type);
        }

        private <T> T read(String path, HttpResponse<String> response, Class<T> type) throws Exception {
            if (response.statusCode() < 200 || response.statusCode() >= 300) {
                throw new IllegalStateException("HTTP " + response.statusCode() + " for " + path + ": "
                    + response.body());
            }
            return json.readValue(response.body(), type);
        }

        private static void ensure(boolean condition) {
            if (!condition) {
                throw new IllegalStateException("Ensure failed");
            }
        }
    }
}
