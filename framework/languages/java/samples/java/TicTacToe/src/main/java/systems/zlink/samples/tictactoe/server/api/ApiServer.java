package systems.zlink.samples.tictactoe.server.api;

import systems.zlink.framework.configuration.ZLinkFrameworkOptions;
import systems.zlink.samples.tictactoe.server.api.handlers.AuthenticatePlayerHandler;
import systems.zlink.samples.tictactoe.server.api.handlers.CreateGameHttpHandler;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.server.configuration.SampleTopology;

public final class ApiServer {
    private ApiServer() {
    }

    public static void configure(ZLinkFrameworkOptions options) {
        options.addClientServerChannel(SampleNames.ApiChannel, channel -> {
            channel.enableServer(server -> server.bind(SampleTopology.ApiEndpoint));
            channel.enableClient(client -> client.useManualConnections(
                endpoints -> endpoints.connect(SampleTopology.ApiEndpoint)));
            channel.addRequestHandler(
                AuthenticatePlayerHandler.class,
                String.class,
                String.class,
                "AuthenticatePlayer");
            channel.addRequestHandler(
                CreateGameHttpHandler.class,
                String.class,
                String.class,
                "CreateGame");
        });
    }
}
