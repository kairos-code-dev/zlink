package systems.zlink.samples.tictactoe.sessiongateway.server.api;

import systems.zlink.framework.configuration.ZLinkFrameworkOptions;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleNames;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleTopology;

public final class ApiServer {
    private ApiServer() {
    }

    public static void configure(ZLinkFrameworkOptions options) {
        options.addHandlersFromPackageOf(ApiServer.class);
        options.addClientServerChannel(SampleNames.ApiChannel, channel -> {
            channel.enableServer(server -> server.bind(SampleTopology.ApiEndpoint));
            channel.enableClient(client -> client.useManualConnections(
                endpoints -> endpoints.connect(SampleTopology.ApiEndpoint)));
            channel.addHandlerGroup("api");
        });
    }
}
