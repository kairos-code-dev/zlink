package systems.zlink.samples.bingo.client.configuration;

import java.io.Reader;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Properties;

public final class SampleTopology {
    public static String SessionAStreamEndpoint;
    public static String SessionBStreamEndpoint;

    private SampleTopology() {
    }

    public static void configure(String[] args) {
        if (args.length != 2 || !"--config".equals(args[0]) || args[1].isBlank()) {
            throw new IllegalArgumentException("Usage: Client --config <path>");
        }
        Properties properties = new Properties();
        try (Reader reader = Files.newBufferedReader(Path.of(args[1]))) {
            properties.load(reader);
        } catch (Exception error) {
            throw new IllegalStateException("Could not load Bingo client config.", error);
        }
        SessionAStreamEndpoint = required(properties, "sessionAStreamEndpoint");
        SessionBStreamEndpoint = required(properties, "sessionBStreamEndpoint");
    }

    private static String required(Properties properties, String name) {
        String value = properties.getProperty(name);
        if (value == null || value.isBlank()) {
            throw new IllegalArgumentException("Missing Bingo client config: " + name);
        }
        return value;
    }
}
