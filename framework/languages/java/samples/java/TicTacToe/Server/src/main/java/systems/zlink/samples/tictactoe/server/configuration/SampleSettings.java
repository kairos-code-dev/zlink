package systems.zlink.samples.tictactoe.server.configuration;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Properties;

public record SampleSettings(
    String apiBindUrl,
    String apiPublicUrl,
    String apiChannelEndpoint,
    String playChannelEndpoint,
    String playEndpoint,
    String spotEndpoint,
    String logDirectory) {
    public static SampleSettings createDefault() {
        return new SampleSettings(
            "http://127.0.0.1:18080",
            "http://127.0.0.1:18080",
            "tcp://127.0.0.1:47201",
            "tcp://127.0.0.1:47203",
            "tcp://127.0.0.1:47202",
            "tcp://127.0.0.1:47205",
            "logs/tictactoe");
    }

    public static SampleSettings load(String[] args) {
        SampleSettings defaults = createDefault();
        SampleSettings configured = fromProperties(readOption(args, "--config", null), defaults);
        return fromArgs(args, configured);
    }

    private static SampleSettings fromArgs(String[] args, SampleSettings defaults) {
        return new SampleSettings(
            readOption(args, "--api-bind", defaults.apiBindUrl()),
            readOption(args, "--api-url", readOption(args, "--api-bind", defaults.apiPublicUrl())),
            readOption(args, "--api-channel-endpoint", defaults.apiChannelEndpoint()),
            readOption(args, "--play-channel-endpoint", defaults.playChannelEndpoint()),
            readOption(args, "--play-endpoint", defaults.playEndpoint()),
            readOption(args, "--spot-endpoint", defaults.spotEndpoint()),
            readOption(args, "--log-dir", defaults.logDirectory()));
    }

    private static SampleSettings fromProperties(String path, SampleSettings defaults) {
        if (path == null || path.isBlank()) {
            return defaults;
        }
        Properties properties = new Properties();
        try (var input = Files.newInputStream(Path.of(path))) {
            properties.load(input);
        } catch (IOException error) {
            throw new IllegalArgumentException("Failed to read config file: " + path, error);
        }
        return new SampleSettings(
            properties.getProperty("sample.apiBindUrl", defaults.apiBindUrl()),
            properties.getProperty("sample.apiPublicUrl", defaults.apiPublicUrl()),
            properties.getProperty("sample.apiChannelEndpoint", defaults.apiChannelEndpoint()),
            properties.getProperty("sample.playChannelEndpoint", defaults.playChannelEndpoint()),
            properties.getProperty("sample.playEndpoint", defaults.playEndpoint()),
            properties.getProperty("sample.spotEndpoint", defaults.spotEndpoint()),
            properties.getProperty("sample.logDirectory", defaults.logDirectory()));
    }

    public int apiHttpPort() {
        return java.net.URI.create(apiBindUrl).getPort();
    }

    private static String readOption(String[] args, String name, String defaultValue) {
        for (int index = 0; index < args.length; index++) {
            if (!args[index].equals(name)) {
                continue;
            }
            if (index + 1 >= args.length) {
                throw new IllegalArgumentException("Missing value for '" + name + "'.");
            }
            return args[index + 1];
        }
        return defaultValue;
    }
}
