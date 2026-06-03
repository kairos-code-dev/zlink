package systems.zlink.samples.bingo.server.api;

import systems.zlink.framework.ZLinkFramework;
import systems.zlink.samples.bingo.server.api.handlers.AuthenticatePlayerHandler;
import systems.zlink.samples.bingo.server.api.handlers.MatchBingoHandler;
import systems.zlink.samples.bingo.shared.configuration.SampleNames;
import systems.zlink.samples.bingo.shared.configuration.SampleTopology;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class ApiServerHostFactory {
    private ApiServerHostFactory() {
    }

    public static ZLinkFramework start() {
        return ZLinkFramework.start(options -> {
            options.useDiscovery(discovery -> discovery.add(SampleTopology.RegistryRouterEndpoint));
            options.addClientServerChannel(SampleNames.ApiChannel, channel -> {
                channel.enableServer(server -> server.bind(SampleTopology.ApiChannelEndpoint));
                channel.addRequestHandler(
                    AuthenticatePlayerHandler.class,
                    Messages.AuthenticatePlayerReq.class,
                    Messages.AuthenticatePlayerRes.class,
                    "AuthenticatePlayer");
                channel.addRequestHandler(
                    MatchBingoHandler.class,
                    Messages.MatchBingoApiReq.class,
                    Messages.MatchBingoApiRes.class,
                    "MatchBingo");
            });
            options.addClientServerChannel(SampleNames.PlayChannel, channel -> channel.enableClient());
        });
    }
}
