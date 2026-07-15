package systems.zlink.samples.tictactoe.server.configuration;

import java.util.List;
import org.springframework.boot.context.properties.ConfigurationProperties;

@ConfigurationProperties("sample")
public record ApiSettings(
    String apiBindUrl,
    String apiChannelEndpoint,
    String playChannelEndpoint,
    List<String> playChannelEndpoints,
    String logDirectory) implements SampleLogSettings {

    public ApiSettings {
        require(apiBindUrl, "apiBindUrl");
        require(apiChannelEndpoint, "apiChannelEndpoint");
        require(playChannelEndpoint, "playChannelEndpoint");
        if (playChannelEndpoints == null || playChannelEndpoints.isEmpty()) {
            throw new IllegalArgumentException("sample.playChannelEndpoints is required");
        }
        require(logDirectory, "logDirectory");
    }

    public int apiHttpPort() {
        return java.net.URI.create(apiBindUrl).getPort();
    }

    public int playIndex() {
        int index = playChannelEndpoints.indexOf(playChannelEndpoint);
        return index >= 0 ? index : 0;
    }

    private static void require(String value, String name) {
        if (value == null || value.isBlank()) {
            throw new IllegalArgumentException("sample." + name + " is required");
        }
    }
}
