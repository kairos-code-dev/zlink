package systems.zlink.samples.tictactoe.server.api;

import systems.zlink.framework.configuration.ZLinkFrameworkOptions;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.server.configuration.SampleTopology;

public final class ApiServer {
    private ApiServer() {
    }

    public static void configure(ZLinkFrameworkOptions options) {
        options.codecs().addJson();
        options.addHandlersFromPackageOf(ApiServer.class);
        options.addClientServerChannel(SampleNames.ApiChannel, channel -> {
            channel.enableServer(server -> server.bind(SampleTopology.ApiEndpoint));
            channel.addHandlerGroup("api");
        });
        options.addClientServerChannel(SampleNames.PlayChannel, channel ->
            channel.enableClient(client -> client.useManualConnections(
                endpoints -> endpoints.connect(SampleTopology.PlayChannelEndpoint))));
    }
}
