package systems.zlink.samples.bingo.server.api;

import systems.zlink.framework.ZLinkFramework;
import systems.zlink.samples.bingo.shared.configuration.SampleNames;
import systems.zlink.samples.bingo.shared.configuration.SampleTopology;

public final class ApiServerHostFactory {
    private ApiServerHostFactory() {
    }

    public static ZLinkFramework start() {
        return ZLinkFramework.start(options -> {
            options.useDiscovery(discovery -> discovery.add(SampleTopology.RegistryRouterEndpoint));
            options.addHandlersFromPackageOf(ApiServerHostFactory.class);
            options.addClientServerChannel(SampleNames.ApiChannel, channel -> {
                channel.enableServer(server -> server.bind(SampleTopology.ApiChannelEndpoint));
                channel.addHandlerGroup("api");
            });
            options.addClientServerChannel(SampleNames.PlayChannel, channel -> channel.enableClient());
        });
    }
}
