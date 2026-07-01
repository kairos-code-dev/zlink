package systems.zlink.e2e.registrymessaging.client.Support;

import java.io.IOException;
import java.net.ServerSocket;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import static java.util.Map.entry;
import java.util.concurrent.TimeUnit;

public final class DynamicClusterLauncher implements AutoCloseable {
    private final List<DynamicProcess> processes = new ArrayList<>();
    private final Path logDir;
    private final String buildDir;
    private final HttpClient healthClient = HttpClient.newBuilder()
        .connectTimeout(Duration.ofSeconds(1))
        .build();
    private String registryRouterEndpoint = "";

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
            launcher.registryRouterEndpoint = pickEndpoint();
            String registryHttp = pickHttpUrl();
            DynamicProcess registry = launcher.startProcess(
                scenarioName + "-registry",
                launcher.registryBinary(),
                Map.of(
                    "ZLINK_JAVA_E2E_REGISTRY_PUB", pickEndpoint(),
                    "ZLINK_JAVA_E2E_REGISTRY_ROUTER", launcher.registryRouterEndpoint,
                    "ZLINK_JAVA_E2E_HTTP_PORT", portOf(registryHttp),
                    "ZLINK_JAVA_E2E_LOG_DIR", launcher.logDir.toString()),
                registryHttp);
            registry.waitReady(launcher.healthClient);
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
                entry("ZLINK_JAVA_E2E_REGISTRY_ROUTER", registryRouterEndpoint),
                entry("ZLINK_JAVA_E2E_LOG_DIR", logDir.toString())),
            httpUrl);
        process.waitReady(healthClient);
        return new DynamicProvider(process, httpUrl, channelEndpoint);
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

    private String registryBinary() {
        return buildDir + "/Server-Registry/install/registry-messaging-registry/bin/registry-messaging-registry";
    }

    private String providerBinary() {
        return buildDir + "/Server-Provider/install/registry-messaging-provider/bin/registry-messaging-provider";
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

    public static final class DynamicProcess {
        private final Process process;
        private final String httpUrl;
        private boolean stopped;

        DynamicProcess(Process process, String httpUrl) {
            this.process = process;
            this.httpUrl = httpUrl;
        }

        void waitReady(HttpClient client) {
            long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(60);
            while (System.nanoTime() < deadline) {
                if (!process.isAlive()) {
                    throw new IllegalStateException("process exited before readiness: " + process.exitValue());
                }
                try {
                    HttpRequest request = HttpRequest.newBuilder(URI.create(httpUrl + "/health"))
                        .timeout(Duration.ofMillis(300))
                        .GET()
                        .build();
                    HttpResponse<Void> response = client.send(request, HttpResponse.BodyHandlers.discarding());
                    if (response.statusCode() >= 200 && response.statusCode() < 300) {
                        return;
                    }
                } catch (IOException | InterruptedException error) {
                    if (error instanceof InterruptedException) {
                        Thread.currentThread().interrupt();
                        throw new IllegalStateException("interrupted while waiting for " + httpUrl, error);
                    }
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
