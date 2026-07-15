package systems.zlink.e2e.registrymessaging.client.Support;

import java.io.IOException;
import java.net.ServerSocket;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import static java.util.Map.entry;
import java.util.concurrent.TimeUnit;
import java.util.function.Predicate;
import systems.zlink.httpclient.ZLinkHttpClient;

public final class DynamicClusterLauncher implements AutoCloseable {
    private final List<DynamicProcess> processes = new ArrayList<>();
    private final Path logDir;
    private final String buildDir;

    private DynamicClusterLauncher(Path logDir, String buildDir) {
        this.logDir = logDir;
        this.buildDir = buildDir;
    }

    public static DynamicClusterLauncher start(String scenarioName) {
        DynamicClusterLauncher launcher = new DynamicClusterLauncher(
            Path.of(ClientOptions.get("ZLINK_JAVA_E2E_LOG_DIR", "logs")),
            ClientOptions.get("ZLINK_JAVA_E2E_BUILD_DIR",
                System.getProperty("user.home") + "/.cache/zlink/java-e2e/RegistryMessaging"));
        try {
            return launcher;
        } catch (RuntimeException error) {
            launcher.close();
            throw error;
        }
    }

    public DynamicProvider startProvider(String name, String rid) {
        return startProvider(name, rid, rid, "");
    }

    public DynamicProvider startProvider(String name, String rid, String instanceId, String weight) {
        String httpUrl = pickHttpUrl();
        String channelEndpoint = pickEndpoint();
        DynamicProcess process = startProcess(
            name,
            providerBinary(),
            Map.ofEntries(
                entry("ZLINK_JAVA_E2E_PROVIDER_RID", rid),
                entry("ZLINK_JAVA_E2E_PROVIDER_INSTANCE", instanceId),
                entry("ZLINK_JAVA_E2E_API_WEIGHT", weight),
                entry("ZLINK_JAVA_E2E_API_ENDPOINT", channelEndpoint),
                entry("ZLINK_JAVA_E2E_API_MANUAL_ENDPOINT", channelEndpoint),
                entry("ZLINK_JAVA_E2E_ROUTE_ENDPOINT", pickEndpoint()),
                entry("ZLINK_JAVA_E2E_ROUTE_PEERS", ""),
                entry("ZLINK_JAVA_E2E_WORKFLOW_ENDPOINT", ""),
                entry("ZLINK_JAVA_E2E_HTTP_PORT", portOf(httpUrl)),
                entry("ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT",
                    ClientOptions.get("ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT")),
                entry("ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX",
                    ClientOptions.get("ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX")),
                entry("ZLINK_JAVA_E2E_LOG_DIR", logDir.toString())),
            httpUrl);
        process.waitReady();
        return new DynamicProvider(process, httpUrl, channelEndpoint);
    }

    public DynamicConsumer startConsumer(String name) {
        String httpUrl = pickHttpUrl();
        DynamicProcess process = startProcess(
            name,
            consumerBinary(),
            Map.ofEntries(
                entry("ZLINK_JAVA_E2E_CONSUMER_NAME", name),
                entry("ZLINK_JAVA_E2E_CONSUMER_MODE", "discovery"),
                entry("ZLINK_JAVA_E2E_PROVIDER_ENDPOINTS", ""),
                entry("ZLINK_JAVA_E2E_HTTP_PORT", portOf(httpUrl)),
                entry("ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT",
                    ClientOptions.get("ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT")),
                entry("ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX",
                    ClientOptions.get("ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX")),
                entry("ZLINK_JAVA_E2E_LOG_DIR", logDir.toString())),
            httpUrl);
        process.waitReady();
        return new DynamicConsumer(process, httpUrl);
    }

    public void waitPeerEndpoint(ZLinkHttpClient consumer, String endpoint) {
        waitPeers(
            consumer,
            peers -> java.util.Arrays.stream(peers).anyMatch(peer -> endpoint.equals(peer.get("endpoint"))),
            "peer endpoint " + endpoint);
    }

    public void waitPeerEndpointAbsent(ZLinkHttpClient consumer, String endpoint) {
        waitPeers(
            consumer,
            peers -> java.util.Arrays.stream(peers).noneMatch(peer -> endpoint.equals(peer.get("endpoint"))),
            "peer endpoint removal " + endpoint);
    }

    public void waitPeerCount(ZLinkHttpClient consumer, int count) {
        waitPeers(consumer, peers -> peers.length == count, "peer count " + count);
    }

    public void stop(DynamicProvider provider) {
        provider.process().stop();
        processes.remove(provider.process());
    }

    @Override
    public void close() {
        for (int index = processes.size() - 1; index >= 0; index--) {
            processes.get(index).stop();
        }
        processes.clear();
    }

    private DynamicProcess startProcess(
        String name,
        String binary,
        Map<String, String> environment,
        String httpUrl) {
        ProcessBuilder builder = new ProcessBuilder(binary);
        builder.environment().putAll(environment);
        builder.redirectOutput(logDir.resolve(name + ".stdout.log").toFile());
        builder.redirectError(logDir.resolve(name + ".stderr.log").toFile());
        try {
            Files.createDirectories(logDir);
            DynamicProcess process = new DynamicProcess(builder.start(), httpUrl);
            processes.add(process);
            return process;
        } catch (IOException error) {
            throw new IllegalStateException("failed to start " + name, error);
        }
    }

    private String providerBinary() {
        return buildDir + "/Server-Provider/install/registry-messaging-provider/bin/registry-messaging-provider";
    }

    private String consumerBinary() {
        return buildDir + "/Server-Consumer/install/registry-messaging-consumer/bin/registry-messaging-consumer";
    }

    private static void waitPeers(
        ZLinkHttpClient consumer,
        Predicate<Map<String, Object>[]> predicate,
        String description) {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(45);
        Map<String, Object>[] latest = new Map[0];
        while (System.nanoTime() < deadline) {
            latest = consumer.get("/locations/peers").fetch(Map[].class);
            if (predicate.test(latest)) {
                return;
            }
            DynamicProcess.sleep(100);
        }
        throw new IllegalStateException("timed out waiting for " + description + ": " + java.util.Arrays.toString(latest));
    }

    private static String pickEndpoint() {
        return "tcp://127.0.0.1:" + pickPort();
    }

    private static String pickHttpUrl() {
        return "http://127.0.0.1:" + pickPort();
    }

    private static int pickPort() {
        try (ServerSocket socket = new ServerSocket(0)) {
            socket.setReuseAddress(true);
            return socket.getLocalPort();
        } catch (IOException error) {
            throw new IllegalStateException("failed to reserve port", error);
        }
    }

    private static String portOf(String endpoint) {
        int index = endpoint.lastIndexOf(':');
        return endpoint.substring(index + 1);
    }

    public record DynamicProvider(DynamicProcess process, String httpUrl, String channelEndpoint) {
    }

    public record DynamicConsumer(DynamicProcess process, String httpUrl) {
    }

    public static final class DynamicProcess {
        private final Process process;
        private final String httpUrl;
        private final ZLinkHttpClient healthClient;
        private boolean stopped;

        DynamicProcess(Process process, String httpUrl) {
            this.process = process;
            this.httpUrl = httpUrl;
            this.healthClient = ZLinkHttpClient.create(httpUrl)
                .timeout(Duration.ofMillis(300))
                .build();
        }

        void waitReady() {
            long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(60);
            while (System.nanoTime() < deadline) {
                if (!process.isAlive()) {
                    throw new IllegalStateException("process exited before readiness: " + process.exitValue());
                }
                try {
                    var response = healthClient.get("/health").submitRaw().toCompletableFuture().join();
                    if (response.status() >= 200 && response.status() < 300) {
                        return;
                    }
                } catch (RuntimeException error) {
                    // The process may accept TCP before its HTTP handler is ready.
                }
                sleep(100);
            }
            throw new IllegalStateException("timed out waiting for " + httpUrl);
        }

        void stop() {
            if (stopped) {
                return;
            }
            stopped = true;
            healthClient.close();
            process.destroy();
            try {
                if (!process.waitFor(5, TimeUnit.SECONDS)) {
                    process.destroyForcibly();
                    process.waitFor(5, TimeUnit.SECONDS);
                }
            } catch (InterruptedException error) {
                Thread.currentThread().interrupt();
                process.destroyForcibly();
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
    }
}
