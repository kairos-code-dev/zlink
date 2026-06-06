package systems.zlink.samples.tictactoe.server.api;

import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.server.configuration.SampleLogging;
import systems.zlink.samples.tictactoe.server.configuration.SampleSettings;

public final class ApiServer {
    private ApiServer() {
    }

    public static ZLinkFrameworkConfigurer configure(SampleSettings settings) {
        return options -> {
            SampleLogging.configure(settings, "api");
            options.codecs().addJson();
            options.addHandlersFromPackageOf(ApiServer.class);
            options.addClientServerChannel(SampleNames.ApiChannel, channel -> {
                channel.enableServer(server -> server.bind(settings.apiChannelEndpoint()));
                channel.addHandlerGroup("api");
            });
            options.addClientServerChannel(SampleNames.PlayChannel, channel ->
                channel.enableClient(client -> client.useManualConnections(
                    endpoints -> endpoints.connect(settings.playChannelEndpoint()))));
        };
    }
}
