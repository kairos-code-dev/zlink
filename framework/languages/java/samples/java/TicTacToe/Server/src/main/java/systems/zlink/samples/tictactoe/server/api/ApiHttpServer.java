package systems.zlink.samples.tictactoe.server.api;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.sun.net.httpserver.HttpServer;
import java.io.IOException;
import java.net.InetSocketAddress;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.samples.tictactoe.server.api.handlers.CreateGameHttpHandler;
import systems.zlink.samples.tictactoe.server.configuration.SampleSettings;

public final class ApiHttpServer implements AutoCloseable {
    private final HttpServer server;
    private final ZLinkClient client;
    private final ObjectMapper json = new ObjectMapper();
    private final SampleSettings settings;

    private ApiHttpServer(HttpServer server, ZLinkClient client, SampleSettings settings) {
        this.server = server;
        this.client = client;
        this.settings = settings;
    }

    public static ApiHttpServer start(ZLinkClient client, SampleSettings settings) throws IOException {
        HttpServer server = HttpServer.create(
            new InetSocketAddress("127.0.0.1", settings.apiHttpPort()),
            0);
        ApiHttpServer api = new ApiHttpServer(server, client, settings);
        server.createContext("/games", exchange ->
            CreateGameHttpHandler.handle(exchange, api.client, api.json));
        server.start();
        return api;
    }

    @Override
    public void close() {
        server.stop(0);
    }
}
