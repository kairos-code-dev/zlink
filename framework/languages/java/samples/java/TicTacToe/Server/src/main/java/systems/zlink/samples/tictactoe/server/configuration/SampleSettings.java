package systems.zlink.samples.tictactoe.server.configuration;

public record SampleSettings(
    String apiBindUrl,
    String apiPublicUrl,
    String apiChannelEndpoint,
    String playChannelEndpoint,
    String playRouterEndpoint,
    String playEndpoint,
    String spotEndpoint,
    String logDirectory) {
    private static SampleSettings current = createDefault();

    public static SampleSettings current() {
        return current;
    }

    public static void setCurrent(SampleSettings settings) {
        current = settings;
    }

    public static SampleSettings createDefault() {
        return new SampleSettings(
            "http://127.0.0.1:18080",
            "http://127.0.0.1:18080",
            "tcp://127.0.0.1:47201",
            "tcp://127.0.0.1:47203",
            "tcp://127.0.0.1:47204",
            "tcp://127.0.0.1:47202",
            "tcp://127.0.0.1:47205",
            "logs/tictactoe");
    }

    public static SampleSettings fromArgs(String[] args) {
        SampleSettings defaults = createDefault();
        return new SampleSettings(
            readOption(args, "--api-bind", defaults.apiBindUrl()),
            readOption(args, "--api-url", readOption(args, "--api-bind", defaults.apiPublicUrl())),
            readOption(args, "--api-channel-endpoint", defaults.apiChannelEndpoint()),
            readOption(args, "--play-channel-endpoint", defaults.playChannelEndpoint()),
            readOption(args, "--play-router-endpoint", defaults.playRouterEndpoint()),
            readOption(args, "--play-endpoint", defaults.playEndpoint()),
            readOption(args, "--spot-endpoint", defaults.spotEndpoint()),
            readOption(args, "--log-dir", defaults.logDirectory()));
    }

    public SampleSettings withEphemeralDefaults() {
        int apiPort = SamplePorts.reserve();
        return new SampleSettings(
            "http://127.0.0.1:" + apiPort,
            "http://127.0.0.1:" + apiPort,
            "tcp://127.0.0.1:" + SamplePorts.reserve(),
            "tcp://127.0.0.1:" + SamplePorts.reserve(),
            "tcp://127.0.0.1:" + SamplePorts.reserve(),
            "tcp://127.0.0.1:" + SamplePorts.reserve(),
            "tcp://127.0.0.1:" + SamplePorts.reserve(),
            logDirectory);
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
