package systems.zlink.samples.tictactoe.server.configuration;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;

public final class SampleLogging {
    private SampleLogging() {
    }

    public static void configure(SampleSettings settings, String role) {
        try {
            Files.createDirectories(Path.of(settings.logDirectory()));
            Path.of(settings.logDirectory(), role + ".log").toFile().createNewFile();
            Files.createDirectories(Path.of(flowLogDirectory()));
        } catch (IOException ex) {
            throw new IllegalStateException("Failed to initialize sample log files.", ex);
        }
    }

    public static String flowLogPath(String role) {
        return Path.of(flowLogDirectory(), "flow-" + role + ".log").toString();
    }

    private static String flowLogDirectory() {
        return System.getenv().getOrDefault("TICTACTOE_LOG_DIR", "logs");
    }
}
