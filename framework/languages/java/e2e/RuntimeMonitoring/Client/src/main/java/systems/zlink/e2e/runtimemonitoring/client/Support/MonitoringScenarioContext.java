package systems.zlink.e2e.runtimemonitoring.client.Support;

import java.io.IOException;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.URI;
import java.time.Duration;
import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.TimeUnit;
import java.util.function.IntPredicate;
import systems.zlink.e2e.runtimemonitoring.shared.Contracts;
import systems.zlink.e2e.runtimemonitoring.client.ClientOptions;
import systems.zlink.httpclient.RawHttpResponse;
import systems.zlink.httpclient.ZLinkHttpClient;

public final class MonitoringScenarioContext implements AutoCloseable {
    private final ClientOptions options;
    private final String triggerEndpoint;
    private final String serviceEndpoint;
    private final String serviceBEndpoint;
    private final ZLinkHttpClient trigger;
    private Process restartedServiceB;

    public MonitoringScenarioContext(ClientOptions options) {
        this.options = options;
        triggerEndpoint = options.triggerHttpEndpoint();
        serviceEndpoint = options.serviceHttpEndpoint();
        serviceBEndpoint = options.serviceBHttpEndpoint();
        trigger = ZLinkHttpClient.create(triggerEndpoint).timeout(Duration.ofMinutes(5)).build();
    }

    public String serviceEndpoint() {
        return serviceEndpoint;
    }

    public String serviceBEndpoint() {
        return serviceBEndpoint;
    }

    public Contracts.WorkRes request(String value) {
        Contracts.WorkRes reply = trigger.post("/request")
            .body(new Contracts.WorkReq(value))
            .async(Contracts.WorkRes.class).toCompletableFuture().join().body();
        ensure(reply.value().equals("work:" + value), "reply mismatch for " + value);
        return reply;
    }

    public Contracts.WorkRes requestFromProvider(String value, String providerRid) {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(20);
        Contracts.WorkRes last = null;
        while (System.nanoTime() < deadline) {
            last = request(value);
            if (providerRid.equals(last.providerRid())) {
                return last;
            }
            sleep(200);
        }
        throw new IllegalStateException(
            "request did not reach " + providerRid + "; last=" + last);
    }

    public ValidationResult validation(String name) {
        return trigger.post("/validation/" + name).async(ValidationResult.class).toCompletableFuture().join().body();
    }

    public void post(String baseUrl, String path) {
        RawHttpResponse response = ZLinkHttpClient.create(baseUrl)
            .timeout(Duration.ofSeconds(3))
            .post(path)
            .submitRaw()
            .toCompletableFuture()
            .join();
        ensure(response.status() >= 200 && response.status() < 300,
            "POST " + baseUrl + path + " returned " + response.status() + ": " + response.body());
    }

    public void postBestEffort(String baseUrl, String path) {
        try {
            post(baseUrl, path);
        } catch (RuntimeException ignored) {
        }
    }

    public Contracts.EvidenceSnapshot evidence(String baseUrl) {
        return ZLinkHttpClient.create(baseUrl)
            .timeout(Duration.ofSeconds(3))
            .get("/evidence")
            .async(Contracts.EvidenceSnapshot.class).toCompletableFuture().join().body();
    }

    public int evidenceEntryCount(String baseUrl) {
        return evidence(baseUrl).entries().size();
    }

    public int latestEvidenceCount(
        String baseUrl,
        String surface,
        String event,
        String field) {
        int latest = -1;
        for (Contracts.EvidenceEntry entry : evidence(baseUrl).entries()) {
            if (!surface.equals(entry.surface()) || !event.equals(entry.event())) {
                continue;
            }
            int count = countFromDetail(entry.detail(), field);
            if (count >= 0) {
                latest = count;
            }
        }
        ensure(latest >= 0, surface + " evidence has no " + field + " count");
        return latest;
    }

    public void waitForEvidenceCountAfter(
        String baseUrl,
        String surface,
        String event,
        String field,
        int firstEntry,
        IntPredicate expected,
        String failureMessage) {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(10);
        while (System.nanoTime() < deadline) {
            var entries = evidence(baseUrl).entries();
            for (int index = Math.min(firstEntry, entries.size()); index < entries.size(); index++) {
                Contracts.EvidenceEntry entry = entries.get(index);
                int count = countFromDetail(entry.detail(), field);
                if (surface.equals(entry.surface())
                    && event.equals(entry.event())
                    && count >= 0
                    && expected.test(count)) {
                    return;
                }
            }
            sleep(100);
        }
        throw new IllegalStateException(failureMessage);
    }

    public void waitForEvent(String baseUrl, String surface, Set<String> expected) {
        waitForEvent(baseUrl, surface, "", expected);
    }

    public void waitForEvent(
        String baseUrl,
        String surface,
        String sourceName,
        Set<String> expected) {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(20);
        while (System.nanoTime() < deadline) {
            Set<String> observed = events(baseUrl, surface, sourceName);
            if (observed.containsAll(expected)) {
                return;
            }
            sleep(200);
        }
        throw new IllegalStateException(
            "missing " + surface + " events " + expected + " at " + baseUrl
                + "; observed=" + events(baseUrl, surface, sourceName)
                + "; evidence=" + evidence(baseUrl));
    }

    public void waitForAnyEvent(String baseUrl, String surface, Set<String> expected) {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(20);
        while (System.nanoTime() < deadline) {
            Set<String> observed = events(baseUrl, surface, "");
            if (expected.stream().anyMatch(observed::contains)) {
                return;
            }
            sleep(200);
        }
        throw new IllegalStateException(
            "missing any " + surface + " event " + expected + " at " + baseUrl
                + "; observed=" + events(baseUrl, surface, ""));
    }

    public void waitForTriggerEvent(String surface, Set<String> expected) {
        waitForEvent(triggerEndpoint, surface, expected);
    }

    public Set<String> events(String baseUrl, String surface, String sourceName) {
        Set<String> events = new HashSet<>();
        for (Contracts.EvidenceEntry entry : evidence(baseUrl).entries()) {
            if (surface.equals(entry.surface())
                && (sourceName.isBlank() || sourceName.equals(entry.sourceName()))) {
                events.add(entry.event());
            }
        }
        return events;
    }

    public void waitForLocationEventCount(String baseUrl, int expectedCount) {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(30);
        while (System.nanoTime() < deadline) {
            int count = 0;
            for (Contracts.EvidenceEntry entry : evidence(baseUrl).entries()) {
                if ("location".equals(entry.surface())
                    && "TOPOLOGY_CHANGED".equals(entry.event())) {
                    count++;
                }
            }
            if (count >= expectedCount) {
                return;
            }
            sleep(250);
        }
        throw new IllegalStateException(
            "missing location topology continuity at " + baseUrl + "; evidence=" + evidence(baseUrl));
    }

    public void restartServiceB() {
        if (restartedServiceB != null && restartedServiceB.isAlive()) {
            return;
        }
        ProcessBuilder builder = new ProcessBuilder(
            options.filteredServiceBinary(),
            "--config",
            options.filteredServiceConfigPath());
        builder.redirectOutput(new java.io.File(
            options.logDirectory() + "/filtered-service-restart.stdout.log"));
        builder.redirectError(new java.io.File(
            options.logDirectory() + "/filtered-service-restart.stderr.log"));
        try {
            restartedServiceB = builder.start();
        } catch (IOException error) {
            throw new IllegalStateException("failed to restart service-b", error);
        }
    }

    public void shutdownServiceB(String failureMessage) {
        post(serviceBEndpoint, "/shutdown");
        waitForPort(serviceBEndpoint, false, failureMessage);
        if (restartedServiceB != null) {
            waitForExit(restartedServiceB);
        }
    }

    public void waitForPort(String baseUrl, boolean open, String failureMessage) {
        URI uri = URI.create(baseUrl);
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(20);
        while (System.nanoTime() < deadline) {
            if (canConnect(uri.getHost(), uri.getPort()) == open) {
                return;
            }
            sleep(100);
        }
        throw new IllegalStateException(failureMessage);
    }

    public void stopRestartedServiceB() {
        if (restartedServiceB == null) {
            return;
        }
        if (restartedServiceB.isAlive()) {
            postBestEffort(serviceBEndpoint, "/shutdown");
        }
        waitForExit(restartedServiceB);
    }

    private void waitForExit(Process process) {
        try {
            if (!process.waitFor(5, TimeUnit.SECONDS)) {
                process.destroyForcibly();
                process.waitFor();
            }
            if (process == restartedServiceB) {
                restartedServiceB = null;
            }
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("interrupted while waiting for restarted service-b", error);
        }
    }

    public void triggerHandshakeFailure() {
        String endpoint = options.handshakeEndpoint();
        int port = Integer.parseInt(endpoint.substring(endpoint.lastIndexOf(':') + 1));
        for (int index = 0; index < 5; index++) {
            try (Socket socket = new Socket()) {
                socket.connect(new InetSocketAddress("127.0.0.1", port), 500);
                OutputStream output = socket.getOutputStream();
                output.write(("invalid-zmtp-handshake-" + index).getBytes(java.nio.charset.StandardCharsets.UTF_8));
                output.flush();
            } catch (IOException ignored) {
                // Rejection is the event this probe asks the service to report.
            }
            sleep(100);
        }
    }

    @Override
    public void close() {
        stopRestartedServiceB();
        trigger.close();
    }

    public static void ensure(boolean condition, String message) {
        if (!condition) {
            throw new IllegalStateException(message);
        }
    }

    private static int countFromDetail(String detail, String field) {
        String prefix = field + "=";
        for (String part : detail.split("\\|")) {
            if (part.startsWith(prefix)) {
                return Integer.parseInt(part.substring(prefix.length()));
            }
        }
        return -1;
    }

    private static boolean canConnect(String host, int port) {
        try (Socket socket = new Socket()) {
            socket.connect(new InetSocketAddress(host, port), 200);
            return true;
        } catch (IOException error) {
            return false;
        }
    }

    private static void sleep(long millis) {
        try {
            Thread.sleep(millis);
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("interrupted", error);
        }
    }

    public record ValidationResult(boolean rejected, String message) {
    }
}
