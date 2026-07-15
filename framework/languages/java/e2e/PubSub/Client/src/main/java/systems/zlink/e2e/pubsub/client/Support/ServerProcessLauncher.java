package systems.zlink.e2e.pubsub.client.Support;

import java.io.IOException;
import java.nio.file.Path;
import java.time.Duration;
import java.util.Map;
import java.util.concurrent.TimeUnit;

public final class ServerProcessLauncher {
    private static final Duration START_TIMEOUT = Duration.ofSeconds(30);

    private final ClientOptions options;
    private final PubSubHttpClient http;

    ServerProcessLauncher(ClientOptions options, PubSubHttpClient http) {
        this.options = options;
        this.http = http;
    }

    public ManagedProcess startSubscriber(String rid, String topics, String httpEndpoint) {
        Map<String, String> env = Map.of(
            "ZLINK_JAVA_E2E_SUBSCRIBER_RID", rid,
            "ZLINK_JAVA_E2E_TOPICS", topics,
            "ZLINK_JAVA_E2E_HTTP_ENDPOINT", httpEndpoint,
            "ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT", options.redisLocationEndpoint(),
            "ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX", options.locationKeyPrefix(),
            "ZLINK_JAVA_E2E_LOG_DIR", options.logDir());
        ManagedProcess process = start(rid, subscriberBin(), env);
        waitHealthy(rid, httpEndpoint);
        return process;
    }

    public ManagedProcess startPublisher(String name) {
        Map<String, String> env = Map.of(
            "ZLINK_JAVA_E2E_PUBLISHER_HTTP", options.publisherHttp(),
            "ZLINK_JAVA_E2E_PUBLISHER_ENDPOINT", options.publisherEndpoint(),
            "ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT", options.redisLocationEndpoint(),
            "ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX", options.locationKeyPrefix(),
            "ZLINK_JAVA_E2E_LOG_DIR", options.logDir());
        ManagedProcess process = start(name, publisherBin(), env);
        waitHealthy(name, options.publisherHttp());
        return process;
    }

    public void waitStopped(String name, String endpoint) {
        long deadline = System.nanoTime() + START_TIMEOUT.toNanos();
        while (System.nanoTime() < deadline) {
            if (!isHealthy(endpoint)) {
                return;
            }
            ScenarioAssert.sleep(100);
        }
        throw new IllegalStateException("timed out waiting for " + name + " to stop at " + endpoint);
    }

    private ManagedProcess start(String name, Path bin, Map<String, String> env) {
        try {
            ProcessBuilder builder = new ProcessBuilder(bin.toString());
            builder.environment().putAll(env);
            builder.redirectOutput(Path.of(options.logDir(), name + ".stdout.log").toFile());
            builder.redirectError(Path.of(options.logDir(), name + ".stderr.log").toFile());
            return new ManagedProcess(name, builder.start());
        } catch (IOException error) {
            throw new IllegalStateException("failed to start " + name + " at " + bin, error);
        }
    }

    private void waitHealthy(String name, String endpoint) {
        long deadline = System.nanoTime() + START_TIMEOUT.toNanos();
        while (System.nanoTime() < deadline) {
            if (isHealthy(endpoint)) {
                return;
            }
            ScenarioAssert.sleep(100);
        }
        throw new IllegalStateException("timed out waiting for " + name + " health at " + endpoint);
    }

    private boolean isHealthy(String endpoint) {
        return http.isHealthy(endpoint, Duration.ofMillis(300));
    }

    private Path publisherBin() {
        return Path.of(options.buildDir(), "Server-Publisher", "install",
            "pub-sub-publisher", "bin", "pub-sub-publisher");
    }

    private Path subscriberBin() {
        return Path.of(options.buildDir(), "Server-Subscriber", "install",
            "pub-sub-subscriber", "bin", "pub-sub-subscriber");
    }

    public static final class ManagedProcess implements AutoCloseable {
        private final String name;
        private final Process process;

        private ManagedProcess(String name, Process process) {
            this.name = name;
            this.process = process;
        }

        @Override
        public void close() {
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
