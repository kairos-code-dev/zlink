package systems.zlink.e2e.storefailure.client.support;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import systems.zlink.e2e.storefailure.shared.Contracts;
import systems.zlink.e2e.storefailure.shared.Wait;

public final class ClientContext implements AutoCloseable {
    private static final ObjectMapper JSON = new ObjectMapper();
    private static final int STORE_DELAY_MILLISECONDS = 1200;

    private final ClientOptions options;
    private final StoreFailureHttpClient http;

    public ClientContext(ClientOptions options) {
        this.options = options;
        this.http = new StoreFailureHttpClient(options.consumerHttpEndpoint());
    }

    public DiscoveryApiResult runStoreFailureBaseline() {
        waitForLivePeerRows();
        waitForHealthyStatus();
        return requestUntilAnyProvider();
    }

    public DiscoveryApiResult runStoreFailurePollingFallback() {
        waitForPollingOnlyStatus();
        waitForLivePeerRows();
        for (String absentRid : options.expectedAbsentRids()) {
            waitForPeerRowsExcluding(absentRid, Duration.ofMillis(options.locationPollingMillis() * 8
                + options.locationLeaseTtlMillis()));
        }
        return requestUntilAnyProvider();
    }

    public DiscoveryApiResult runStoreFailureCrashLeaseExpiry() {
        waitForPeerRowsExcluding(options.deadRid(), Duration.ofMillis(options.locationLeaseTtlMillis() * 2
            + options.locationPollingMillis() * 4));
        Wait.sleep(Duration.ofMillis(options.locationPollingMillis() * 4));
        return requestSurvivorOnly();
    }

    public DiscoveryApiResult runStoreFailureGracefulRemoval() {
        Instant started = Instant.now();
        waitForPeerRowsExcluding(options.deadRid(), Duration.ofMillis(options.locationLeaseTtlMillis()));
        Duration elapsed = Duration.between(started, Instant.now());
        ScenarioAssert.that(elapsed.compareTo(Duration.ofMillis(options.locationLeaseTtlMillis())) < 0,
            "SF-C2 row removal did not beat owner lease TTL: " + elapsed);
        Wait.sleep(Duration.ofMillis(options.locationPollingMillis() * 4));
        return requestSurvivorOnly();
    }

    public DiscoveryApiResult runStoreFailureFailStaticOutage() {
        DiscoveryApiResult result = driveRequests(
            "sf-b1-outage",
            Duration.ofMillis(Math.max(1, (long) (options.locationLeaseTtlMillis() * 0.7))),
            "SF-B1");
        waitForUnhealthyStatus("SF-B1");
        waitForOwnerLeaseFailure("SF-B1");
        return result;
    }

    public DiscoveryApiResult runStoreFailureGraceExceeded() {
        DiscoveryApiResult result = driveRequests(
            "sf-b2-grace",
            Duration.ofMillis(options.locationStoreFailureGraceMillis() + options.locationHeartbeatMillis() * 2),
            "SF-B2");
        waitForUnhealthyStatus("SF-B2");
        return result;
    }

    public DiscoveryApiResult runStoreFailureRecovered(String scenarioName) {
        waitForRecoveredStatus(scenarioName);
        return requestUntilAnyProvider();
    }

    public DiscoveryApiResult runStoreFailureRecoveredWithPeers(String scenarioName) {
        waitForRecoveredStatus(scenarioName);
        waitForLivePeerRows();
        return requestUntilAnyProvider();
    }

    public DiscoveryApiResult runStoreFailureShortOutageTraffic() {
        return driveRequests(
            "sf-d1",
            Duration.ofMillis(options.locationLeaseTtlMillis() * 2),
            "SF-D1");
    }

    public DiscoveryApiResult runStoreFailureLongOutageRecovery() {
        DiscoveryApiResult traffic = driveTolerantRequests(
            "sf-d2",
            Duration.ofMillis(options.locationLeaseTtlMillis() * 2 + options.locationHeartbeatMillis() * 4),
            "SF-D2");
        ScenarioAssert.that(traffic.providers().stream().anyMatch("api-a"::equals),
            "SF-D2 no request was served by the surviving provider");
        waitForLivePeerRows();
        waitForPeerRowsExcluding(
            options.deadRid(),
            Duration.ofMillis(options.locationLeaseTtlMillis() * 2 + options.locationHeartbeatMillis() * 4));
        return requestSurvivorOnly("SF-D2", "sf-d2-after", 8);
    }

    public DiscoveryApiResult runStoreFailureStatusHealthy() {
        JsonNode status = waitForHealthyStatus();
        ScenarioAssert.that(!status.path("lastRefreshAt").asText("").isBlank(),
            "SF-D3 healthy status did not expose last refresh time");
        ScenarioAssert.that(!status.path("ownerLeaseRenewedAt").asText("").isBlank(),
            "SF-D3 healthy status did not expose owner lease renewal time");
        return requestUntilAnyProvider();
    }

    public DiscoveryApiResult runStoreFailureStatusOutage() {
        JsonNode status = waitForStatus(
            Duration.ofMillis(options.locationHeartbeatMillis() * 8),
            current -> !current.path("storeHealthy").asBoolean(true)
                && !current.path("ownerLeaseHealthy").asBoolean(true)
                && !current.path("lastError").asText("").isBlank(),
            "SF-D3 outage did not surface as unhealthy status with owner lease failure");
        ScenarioAssert.that(!status.path("watchEnabled").asBoolean(true),
            "SF-D3 outage status did not expose polling-mode watch state");
        ScenarioAssert.that(status.path("pollingIntervalMillis").asLong(0) > 0,
            "SF-D3 outage status did not expose polling interval");
        return new DiscoveryApiResult(Set.of());
    }

    public DiscoveryApiResult runStoreFailureStatusRecovered() {
        JsonNode status = waitForRecoveredStatus("SF-D3");
        ScenarioAssert.that(!status.path("lastRefreshAt").asText("").isBlank(),
            "SF-D3 recovered status did not expose last refresh time");
        ScenarioAssert.that(!status.path("ownerLeaseRenewedAt").asText("").isBlank(),
            "SF-D3 recovered status did not expose owner lease renewal time");
        return requestUntilAnyProvider();
    }

    public DiscoveryApiResult runStoreFailureStoreDelayNonBlocking() {
        waitForLivePeerRows();
        List<Long> baseline = measureRequests("sf-e1-baseline", 10);
        long baselineP99 = percentileMillis(baseline, 0.99);
        setStoreDelay(STORE_DELAY_MILLISECONDS);
        try {
            CompletableFuture<Long> delayedStoreRead = CompletableFuture.supplyAsync(this::measureStoreRead);
            Wait.sleep(Duration.ofMillis(150));
            List<Long> concurrent = measureRequests("sf-e1-concurrent", 12);
            long delayedStoreReadMs = delayedStoreRead.join();
            long concurrentP99 = percentileMillis(concurrent, 0.99);
            long budget = Math.max(baselineP99 * 8, 750);

            ScenarioAssert.that(delayedStoreReadMs >= STORE_DELAY_MILLISECONDS * 0.75,
                "SF-E1 delayed store read finished too quickly: " + delayedStoreReadMs + " ms");
            ScenarioAssert.that(concurrentP99 <= budget,
                "SF-E1 unrelated request p99 grew too much during store delay. baseline="
                    + baselineP99 + " ms concurrent=" + concurrentP99 + " ms budget=" + budget + " ms");

            DiscoveryApiResult recovery = requestUntilAnyProvider("SF-E1", "sf-e1-recovery", 1);
            System.out.println("scenario SF-E1 passed");
            return recovery;
        } finally {
            setStoreDelay(0);
        }
    }

    private DiscoveryApiResult requestUntilExpectedProviders() {
        String consumerEndpoint = options.consumerHttpEndpoint();
        if (consumerEndpoint == null || consumerEndpoint.isBlank()) {
            throw new IllegalStateException("consumer HTTP endpoint is required");
        }
        List<String> expected = options.expectedRids();
        Set<String> providers = new HashSet<>();
        for (int index = 0; index < 120 && !providers.containsAll(expected); index++) {
            Contracts.ProfileReq request =
                new Contracts.ProfileReq("msg-" + index, "marker-" + index);
            try {
                String body = http.postJson("/profile/request/wait", request);
                Contracts.ProfileRes reply = JSON.readValue(body, Contracts.ProfileRes.class);
                ScenarioAssert.that(reply.value().equals("profile:msg-" + index), "reply payload mismatch");
                providers.add(reply.providerRid());
            } catch (Exception error) {
                throw new IllegalStateException("consumer request failed", error);
            }
        }
        ScenarioAssert.that(providers.containsAll(expected),
            "messaging did not reach expected providers " + expected + ": " + providers);
        return new DiscoveryApiResult(Set.copyOf(providers));
    }

    private DiscoveryApiResult requestUntilAnyProvider() {
        return requestUntilAnyProvider("SF-A1", "sf-a1", 60);
    }

    private DiscoveryApiResult requestUntilAnyProvider(String scenarioName, String markerPrefix, int attempts) {
        String consumerEndpoint = options.consumerHttpEndpoint();
        if (consumerEndpoint == null || consumerEndpoint.isBlank()) {
            throw new IllegalStateException("consumer HTTP endpoint is required");
        }
        for (int index = 0; index < attempts; index++) {
            Contracts.ProfileReq request =
                new Contracts.ProfileReq(markerPrefix + "-msg-" + index, markerPrefix + "-marker-" + index);
            try {
                String body = http.postJson("/profile/request/wait", request);
                Contracts.ProfileRes reply = JSON.readValue(body, Contracts.ProfileRes.class);
                ScenarioAssert.that(reply.value().equals("profile:" + markerPrefix + "-msg-" + index),
                    scenarioName + " reply payload mismatch");
                ScenarioAssert.that(options.expectedRids().contains(reply.providerRid()),
                    scenarioName + " unexpected provider " + reply.providerRid());
                return new DiscoveryApiResult(Set.of(reply.providerRid()));
            } catch (Exception error) {
                if (index == attempts - 1) {
                    throw new IllegalStateException(scenarioName + " consumer request failed", error);
                }
            }
        }
        throw new IllegalStateException(scenarioName + " consumer request did not complete");
    }

    private long measureStoreRead() {
        Instant started = Instant.now();
        waitForLivePeerRows();
        return Duration.between(started, Instant.now()).toMillis();
    }

    private List<Long> measureRequests(String markerPrefix, int count) {
        String consumerEndpoint = options.consumerHttpEndpoint();
        if (consumerEndpoint == null || consumerEndpoint.isBlank()) {
            throw new IllegalStateException("consumer HTTP endpoint is required");
        }
        List<Long> timings = new ArrayList<>(count);
        for (int index = 0; index < count; index++) {
            Contracts.ProfileReq request =
                new Contracts.ProfileReq(markerPrefix + "-msg-" + index, markerPrefix + "-marker-" + index);
            Instant started = Instant.now();
            try {
                String body = http.postJson("/profile/request", request);
                Contracts.ProfileRes reply = JSON.readValue(body, Contracts.ProfileRes.class);
                ScenarioAssert.that(reply.value().equals("profile:" + markerPrefix + "-msg-" + index),
                    "SF-E1 reply payload mismatch");
                ScenarioAssert.that(options.expectedRids().contains(reply.providerRid()),
                    "SF-E1 unexpected provider " + reply.providerRid());
            } catch (Exception error) {
                throw new IllegalStateException("SF-E1 request failed", error);
            }
            timings.add(Duration.between(started, Instant.now()).toMillis());
        }
        return timings;
    }

    private void setStoreDelay(int delayMilliseconds) {
        try {
            http.postJson("/admin/store-delay", new Contracts.StoreDelayReq(delayMilliseconds));
        } catch (Exception error) {
            throw new IllegalStateException("failed to set store delay", error);
        }
    }

    private static long percentileMillis(List<Long> values, double percentile) {
        List<Long> sorted = new ArrayList<>(values);
        Collections.sort(sorted);
        int index = (int) Math.ceil(percentile * sorted.size()) - 1;
        return sorted.get(Math.max(0, Math.min(index, sorted.size() - 1)));
    }

    private DiscoveryApiResult requestSurvivorOnly() {
        return requestSurvivorOnly("SF-C1", "sf-c1", 12);
    }

    private DiscoveryApiResult requestSurvivorOnly(String scenarioName, String markerPrefix, int count) {
        String consumerEndpoint = options.consumerHttpEndpoint();
        if (consumerEndpoint == null || consumerEndpoint.isBlank()) {
            throw new IllegalStateException("consumer HTTP endpoint is required");
        }
        List<String> survivors = options.expectedRids();
        ScenarioAssert.that(!survivors.isEmpty(), scenarioName + " requires at least one survivor rid");
        Set<String> providers = new HashSet<>();
        Instant started = Instant.now();
        for (int index = 0; index < count; index++) {
            Contracts.ProfileReq request =
                new Contracts.ProfileReq(markerPrefix + "-msg-" + index, markerPrefix + "-marker-" + index);
            try {
                String body = http.postJson("/profile/request", request);
                Contracts.ProfileRes reply = JSON.readValue(body, Contracts.ProfileRes.class);
                ScenarioAssert.that(reply.value().equals("profile:" + markerPrefix + "-msg-" + index),
                    scenarioName + " reply payload mismatch");
                ScenarioAssert.that(survivors.contains(reply.providerRid()),
                    scenarioName + " request reached non-survivor provider " + reply.providerRid());
                ScenarioAssert.that(!reply.providerRid().equals(options.deadRid()),
                    scenarioName + " request reached crashed provider " + options.deadRid());
                providers.add(reply.providerRid());
            } catch (Exception error) {
                throw new IllegalStateException(scenarioName + " survivor request failed", error);
            }
        }
        Duration elapsed = Duration.between(started, Instant.now());
        ScenarioAssert.that(elapsed.compareTo(Duration.ofSeconds(12)) < 0,
            scenarioName + " requests were slow, suggesting repeated routing to the dead provider: " + elapsed);
        return new DiscoveryApiResult(Set.copyOf(providers));
    }

    private DiscoveryApiResult driveRequests(String markerPrefix, Duration window, String scenarioName) {
        String consumerEndpoint = options.consumerHttpEndpoint();
        if (consumerEndpoint == null || consumerEndpoint.isBlank()) {
            throw new IllegalStateException("consumer HTTP endpoint is required");
        }
        Set<String> providers = new HashSet<>();
        Instant deadline = Instant.now().plus(window);
        int index = 0;
        while (Instant.now().isBefore(deadline)) {
            Contracts.ProfileReq request =
                new Contracts.ProfileReq(markerPrefix + "-msg-" + index, markerPrefix + "-marker-" + index);
            try {
                String body = http.postJson("/profile/request", request);
                Contracts.ProfileRes reply = JSON.readValue(body, Contracts.ProfileRes.class);
                ScenarioAssert.that(reply.value().equals("profile:" + markerPrefix + "-msg-" + index),
                    scenarioName + " reply payload mismatch");
                ScenarioAssert.that(options.expectedRids().contains(reply.providerRid()),
                    scenarioName + " unexpected provider " + reply.providerRid());
                providers.add(reply.providerRid());
            } catch (Exception error) {
                throw new IllegalStateException(scenarioName + " request failed during outage", error);
            }
            index++;
            Wait.sleep(Duration.ofMillis(150));
        }
        ScenarioAssert.that(index > 0, scenarioName + " produced no request traffic");
        return new DiscoveryApiResult(Set.copyOf(providers));
    }

    private DiscoveryApiResult driveTolerantRequests(String markerPrefix, Duration window, String scenarioName) {
        String consumerEndpoint = options.consumerHttpEndpoint();
        if (consumerEndpoint == null || consumerEndpoint.isBlank()) {
            throw new IllegalStateException("consumer HTTP endpoint is required");
        }
        Set<String> providers = new HashSet<>();
        Instant deadline = Instant.now().plus(window);
        Instant lastSuccess = Instant.now();
        Duration maxGap = Duration.ZERO;
        int index = 0;
        while (Instant.now().isBefore(deadline)) {
            Contracts.ProfileReq request =
                new Contracts.ProfileReq(markerPrefix + "-msg-" + index, markerPrefix + "-marker-" + index);
            try {
                String body = http.postJson("/profile/request", request);
                Contracts.ProfileRes reply = JSON.readValue(body, Contracts.ProfileRes.class);
                ScenarioAssert.that(reply.value().equals("profile:" + markerPrefix + "-msg-" + index),
                    scenarioName + " reply payload mismatch");
                providers.add(reply.providerRid());
                Duration gap = Duration.between(lastSuccess, Instant.now());
                if (gap.compareTo(maxGap) > 0) {
                    maxGap = gap;
                }
                lastSuccess = Instant.now();
            } catch (Exception ignored) {
                // A request may land on the provider killed during the store outage.
            }
            index++;
            Wait.sleep(Duration.ofMillis(150));
        }
        Duration finalGap = Duration.between(lastSuccess, Instant.now());
        if (finalGap.compareTo(maxGap) > 0) {
            maxGap = finalGap;
        }
        ScenarioAssert.that(!providers.isEmpty(), scenarioName + " produced no successful request traffic");
        ScenarioAssert.that(maxGap.compareTo(Duration.ofMillis(options.locationLeaseTtlMillis() * 2)) < 0,
            scenarioName + " successful traffic stalled for " + maxGap);
        return new DiscoveryApiResult(Set.copyOf(providers));
    }

    private void waitForLivePeerRows() {
        String consumerEndpoint = options.consumerHttpEndpoint();
        Set<String> expectedSet = new HashSet<>(options.expectedRids());
        Set<String> observed = Wait.until(
            Duration.ofSeconds(20),
            "consumer location query missing expected peer rows " + options.expectedRids(),
            () -> {
                try {
                    JsonNode root = JSON.readTree(http.get("/locations/peers"));
                    Set<String> rids = new HashSet<>();
                    for (JsonNode entry : root) {
                        String rid = entry.path("nodeRid").asText("");
                        if (!rid.isBlank()) {
                            rids.add(rid);
                        }
                    }
                    return containsAllRids(rids, expectedSet) ? rids : null;
                } catch (Exception error) {
                    return null;
                }
            });
        ScenarioAssert.that(containsAllRids(observed, expectedSet),
            "location rows missing expected providers " + expectedSet + ": " + observed);
    }

    private void waitForPeerRowsExcluding(String deadRid, Duration timeout) {
        String consumerEndpoint = options.consumerHttpEndpoint();
        Set<String> survivorSet = new HashSet<>(options.expectedRids());
        Set<String> observed = Wait.until(
            timeout,
            "consumer location query still includes removed provider " + deadRid,
            () -> {
                try {
                    JsonNode root = JSON.readTree(http.get("/locations/peers"));
                    Set<String> rids = new HashSet<>();
                    for (JsonNode entry : root) {
                        String rid = entry.path("nodeRid").asText("");
                        if (!rid.isBlank()) {
                            rids.add(rid);
                        }
                    }
                    boolean hasSurvivors = containsAllRids(rids, survivorSet);
                    boolean hasDead = rids.stream().anyMatch(value -> value.equals(deadRid) || value.contains(deadRid));
                    return hasSurvivors && !hasDead ? rids : null;
                } catch (Exception error) {
                    return null;
                }
            });
        ScenarioAssert.that(containsAllRids(observed, survivorSet),
            "location rows missing survivor providers " + survivorSet + ": " + observed);
        ScenarioAssert.that(observed.stream().noneMatch(value -> value.equals(deadRid) || value.contains(deadRid)),
            "location rows still include crashed provider " + deadRid + ": " + observed);
    }

    private JsonNode waitForHealthyStatus() {
        String consumerEndpoint = options.consumerHttpEndpoint();
        long deadline = System.nanoTime() + Duration.ofSeconds(20).toNanos();
        JsonNode status = null;
        Exception lastError = null;
        while (System.nanoTime() < deadline) {
            try {
                status = JSON.readTree(http.get("/locations/status"));
                if (status.path("storeHealthy").asBoolean(false)) {
                    break;
                }
            } catch (Exception error) {
                lastError = error;
            }
            try {
                Thread.sleep(100);
            } catch (InterruptedException error) {
                Thread.currentThread().interrupt();
                throw new IllegalStateException("interrupted while waiting for location status", error);
            }
        }
        if (status == null || !status.path("storeHealthy").asBoolean(false)) {
            throw new IllegalStateException(
                "consumer location runtime status did not become healthy; lastStatus="
                    + status + ", lastError=" + lastError);
        }
        ScenarioAssert.that(status.path("storeHealthy").asBoolean(false), "store status is not healthy");
        ScenarioAssert.that(status.path("ownerLeaseHealthy").asBoolean(false), "owner lease status is not healthy");
        return status;
    }

    private void waitForPollingOnlyStatus() {
        String consumerEndpoint = options.consumerHttpEndpoint();
        JsonNode status = Wait.until(
            Duration.ofSeconds(10),
            "consumer location runtime status did not report polling-only mode",
            () -> {
                JsonNode current = JSON.readTree(http.get("/locations/status"));
                boolean healthy = current.path("storeHealthy").asBoolean(false);
                boolean watchEnabled = current.path("watchEnabled").asBoolean(true);
                long pollingMillis = current.path("pollingIntervalMillis").asLong(0);
                return healthy && !watchEnabled && pollingMillis > 0 ? current : null;
            });
        ScenarioAssert.that(status.path("storeHealthy").asBoolean(false), "SF-A2 store status is not healthy");
        ScenarioAssert.that(!status.path("watchEnabled").asBoolean(true),
            "SF-A2 polling-only consumer unexpectedly reports watch enabled");
    }

    private void waitForUnhealthyStatus(String scenarioName) {
        JsonNode status = waitForStatus(
            Duration.ofMillis(options.locationHeartbeatMillis() * 8),
            current -> !current.path("storeHealthy").asBoolean(true)
                && !current.path("lastError").asText("").isBlank(),
            scenarioName + " did not record store outage");
        ScenarioAssert.that(!status.path("storeHealthy").asBoolean(true),
            scenarioName + " store status is not unhealthy");
    }

    private void waitForOwnerLeaseFailure(String scenarioName) {
        waitForStatus(
            Duration.ofMillis(options.locationHeartbeatMillis() * 8),
            current -> !current.path("ownerLeaseHealthy").asBoolean(true),
            scenarioName + " owner lease heartbeat failure did not surface");
    }

    private JsonNode waitForRecoveredStatus(String scenarioName) {
        return waitForStatus(
            Duration.ofMillis(options.locationHeartbeatMillis() * 10),
            current -> current.path("storeHealthy").asBoolean(false)
                && current.path("ownerLeaseHealthy").asBoolean(false),
            scenarioName + " status did not recover after store outage");
    }

    private JsonNode waitForStatus(
        Duration timeout,
        java.util.function.Predicate<JsonNode> accept,
        String failureMessage) {
        String consumerEndpoint = options.consumerHttpEndpoint();
        return Wait.until(
            timeout,
            failureMessage,
            () -> {
                try {
                    JsonNode current = JSON.readTree(http.get("/locations/status"));
                    return accept.test(current) ? current : null;
                } catch (Exception error) {
                    return null;
                }
            });
    }

    private static boolean containsAllRids(Set<String> observed, Set<String> expected) {
        for (String rid : expected) {
            if (observed.stream().noneMatch(value -> value.equals(rid) || value.contains(rid))) {
                return false;
            }
        }
        return true;
    }

    @Override
    public void close() {
        http.close();
    }

}
