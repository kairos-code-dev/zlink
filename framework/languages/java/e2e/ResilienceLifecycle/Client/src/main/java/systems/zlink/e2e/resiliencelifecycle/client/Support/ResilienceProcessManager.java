package systems.zlink.e2e.resiliencelifecycle.client.Support;

import java.io.IOException;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.URI;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.TimeUnit;
import systems.zlink.httpclient.RawHttpResponse;
import systems.zlink.httpclient.ZLinkHttpClient;

public final class ResilienceProcessManager implements AutoCloseable {
    private static final Duration READY_TIMEOUT = Duration.ofSeconds(60);
    private static final Duration SIGNAL_TIMEOUT = Duration.ofSeconds(30);

    private final ClientOptions options;
    private final List<ManagedProcess> processes = new ArrayList<>();

    public ResilienceProcessManager(ClientOptions options) {
        this.options = options;
    }

    public ManagedProcess startProvider(String name, String rid, String apiEndpoint, String httpEndpoint) {
        Map<String, String> env = Map.of(
            "ZLINK_JAVA_E2E_PROVIDER_RID", rid,
            "ZLINK_JAVA_E2E_API_ENDPOINT", apiEndpoint,
            "ZLINK_JAVA_E2E_HTTP_ENDPOINT", httpEndpoint,
            "ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT", options.redisLocationEndpoint(),
            "ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX", options.locationKeyPrefix(),
            "ZLINK_JAVA_E2E_LOG_DIR", options.logDir());
        ManagedProcess process = start(name, providerBin(), env);
        waitTcp(name + "-api", apiEndpoint, true);
        waitTcp(name + "-http", httpEndpoint, true);
        return process;
    }

    public ManagedProcess startConsumer(
        String name,
        String httpEndpoint,
        String currentHttpA,
        long stormExitDelayMillis,
        String logDir) {
        Map<String, String> env = Map.ofEntries(
            Map.entry("ZLINK_JAVA_E2E_CONSUMER_HTTP_ENDPOINT", httpEndpoint),
            Map.entry("ZLINK_JAVA_E2E_CONTROL_DIR", controlDir().toString()),
            Map.entry("ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT", options.redisLocationEndpoint()),
            Map.entry("ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX", options.locationKeyPrefix()),
            Map.entry("ZLINK_JAVA_E2E_API_A_REPLACEMENT_ENDPOINT", options.apiAReplacementEndpoint()),
            Map.entry("ZLINK_JAVA_E2E_API_B_GREEN_ENDPOINT", options.apiBGreenEndpoint()),
            Map.entry("ZLINK_JAVA_E2E_HTTP_A_ENDPOINT", currentHttpA),
            Map.entry("ZLINK_JAVA_E2E_HTTP_B_ENDPOINT", options.httpBEndpoint()),
            Map.entry("ZLINK_JAVA_E2E_HTTP_B_GREEN_ENDPOINT", options.httpBGreenEndpoint()),
            Map.entry("ZLINK_JAVA_E2E_STORM_EXIT_DELAY_MS", Long.toString(stormExitDelayMillis)),
            Map.entry("ZLINK_JAVA_E2E_LOG_DIR", logDir));
        ManagedProcess process = start(name, consumerBin(), env);
        waitTcp(name + "-http", httpEndpoint, true);
        return process;
    }

    public String reserveHttpEndpoint() {
        try (ServerSocket socket = new ServerSocket(0)) {
            socket.setReuseAddress(true);
            return "http://127.0.0.1:" + socket.getLocalPort();
        } catch (IOException error) {
            throw new IllegalStateException("failed to reserve HTTP endpoint", error);
        }
    }

    public Path controlDir() {
        return Path.of(options.logDir(), "control");
    }

    public void prepareControlDir() {
        try {
            Files.createDirectories(controlDir());
        } catch (IOException error) {
            throw new IllegalStateException("failed to create control dir", error);
        }
    }

    public void waitSignal(String name) {
        Path path = controlDir().resolve(name);
        long deadline = System.nanoTime() + SIGNAL_TIMEOUT.toNanos();
        while (System.nanoTime() < deadline) {
            if (Files.exists(path)) {
                return;
            }
            sleep(100);
        }
        throw new IllegalStateException("timed out waiting for " + path);
    }

    public void touchSignal(String name) {
        try {
            Files.createFile(controlDir().resolve(name));
        } catch (java.nio.file.FileAlreadyExistsException ignored) {
        } catch (IOException error) {
            throw new IllegalStateException("failed to create control signal " + name, error);
        }
    }

    public void waitEndpointDown(String name, String endpoint) {
        waitTcp(name, endpoint, false);
    }

    public void sleep(long millis) {
        sleepMillis(millis);
    }

    public void pauseStore() {
        runStoreCommand("pause", options.storePauseCommand());
    }

    public void unpauseStore() {
        runStoreCommand("resume", options.storeResumeCommand());
    }

    @Override
    public void close() {
        for (int i = processes.size() - 1; i >= 0; i--) {
            processes.get(i).close();
        }
        processes.clear();
    }

    private ManagedProcess start(String name, Path bin, Map<String, String> env) {
        try {
            ProcessBuilder builder = new ProcessBuilder(bin.toString());
            builder.environment().putAll(env);
            builder.redirectOutput(Path.of(options.logDir(), name + ".stdout.log").toFile());
            builder.redirectError(Path.of(options.logDir(), name + ".stderr.log").toFile());
            ManagedProcess process = new ManagedProcess(name, builder.start());
            processes.add(process);
            return process;
        } catch (IOException error) {
            throw new IllegalStateException("failed to start " + name + " at " + bin, error);
        }
    }

    private void runStoreCommand(String action, String commandPath) {
        if (commandPath == null || commandPath.isBlank()) {
            throw new IllegalStateException("RL-C4 requires a store " + action + " command from the runner.");
        }
        runCommand("store " + action, commandPath);
    }

    private void runCommand(String description, String... command) {
        try {
            Process process = new ProcessBuilder(command).start();
            boolean exited = process.waitFor(15, TimeUnit.SECONDS);
            if (!exited || process.exitValue() != 0) {
                throw new IllegalStateException(description + " failed");
            }
        } catch (IOException error) {
            throw new IllegalStateException("failed to run " + description, error);
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("interrupted while running " + description, error);
        }
    }

    private void waitTcp(String name, String endpoint, boolean expectedUp) {
        long deadline = System.nanoTime() + READY_TIMEOUT.toNanos();
        while (System.nanoTime() < deadline) {
            boolean up = canConnect(endpoint);
            if (up == expectedUp) {
                return;
            }
            sleep(100);
        }
        throw new IllegalStateException("timed out waiting for " + name + " up=" + expectedUp + " at " + endpoint);
    }

    private boolean canConnect(String endpoint) {
        if (endpoint.startsWith("tcp://")) {
            try {
                URI uri = URI.create(endpoint);
                try (Socket socket = new Socket(uri.getHost(), uri.getPort())) {
                    return true;
                }
            } catch (Exception ignored) {
                return false;
            }
        }
        try (ZLinkHttpClient http = ZLinkHttpClient.create(endpoint).build()) {
            RawHttpResponse response = http.get("/health")
                .timeout(Duration.ofMillis(300))
                .submitRaw()
                .toCompletableFuture()
                .join();
            return response.status() >= 200 && response.status() < 300;
        } catch (RuntimeException ignored) {
            return false;
        }
    }

    private Path providerBin() {
        return Path.of(options.buildDir(), "Server-Provider", "install",
            "resilience-lifecycle-provider", "bin", "resilience-lifecycle-provider");
    }

    private Path consumerBin() {
        return Path.of(options.buildDir(), "Server-Consumer", "install",
            "resilience-lifecycle-consumer", "bin", "resilience-lifecycle-consumer");
    }

    private static void sleepMillis(long millis) {
        try {
            Thread.sleep(millis);
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("interrupted", error);
        }
    }

    public final class ManagedProcess implements AutoCloseable {
        private final String name;
        private final Process process;
        private boolean closed;

        private ManagedProcess(String name, Process process) {
            this.name = name;
            this.process = process;
        }

        public void killForcibly() {
            if (closed) {
                return;
            }
            closed = true;
            process.destroyForcibly();
            try {
                process.waitFor(5, TimeUnit.SECONDS);
            } catch (InterruptedException error) {
                Thread.currentThread().interrupt();
                throw new IllegalStateException("interrupted while killing " + name, error);
            }
        }

        @Override
        public void close() {
            if (closed) {
                return;
            }
            closed = true;
            if (!process.isAlive()) {
                return;
            }
            process.destroy();
            try {
                if (!process.waitFor(5, TimeUnit.SECONDS)) {
                    process.destroyForcibly();
                    process.waitFor(5, TimeUnit.SECONDS);
                }
            } catch (InterruptedException error) {
                Thread.currentThread().interrupt();
                process.destroyForcibly();
                throw new IllegalStateException("interrupted while stopping " + name, error);
            }
        }
    }
}
