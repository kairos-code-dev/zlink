package systems.zlink.e2e.registrymessaging.client.Support;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.concurrent.TimeUnit;

public final class ScenarioSignals {
    private ScenarioSignals() {
    }

    public static void touch(String path) {
        if (path.isBlank()) {
            return;
        }
        try {
            Files.writeString(Path.of(path), "ready\n");
        } catch (java.io.IOException error) {
            throw new IllegalStateException("failed to write " + path, error);
        }
    }

    public static void waitForFile(String path) {
        if (path.isBlank()) {
            return;
        }
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(20);
        while (System.nanoTime() < deadline) {
            if (Files.exists(Path.of(path))) {
                return;
            }
            sleep(50);
        }
        throw new IllegalStateException("timed out waiting for " + path);
    }

    public static void sleep(long millis) {
        try {
            Thread.sleep(millis);
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("interrupted", error);
        }
    }
}
