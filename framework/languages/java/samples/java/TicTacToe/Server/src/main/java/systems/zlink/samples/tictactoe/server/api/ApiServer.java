package systems.zlink.samples.tictactoe.server.api;

import systems.zlink.framework.codecs.msgpack.ZLinkMessagePackCodec;
import systems.zlink.framework.configuration.ClientServerChannelBuilder;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.samples.tictactoe.server.api.handlers.AuthenticatePlayerHandler;
import systems.zlink.samples.tictactoe.server.configuration.SampleLogging;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.server.configuration.SampleSettings;
import systems.zlink.samples.tictactoe.shared.contracts.AuthenticatePlayerReq;
import systems.zlink.samples.tictactoe.shared.contracts.AuthenticatePlayerRes;

public final class ApiServer {
    private ApiServer() {
    }

    public static ZLinkFrameworkConfigurer configure(SampleSettings settings) {
        return options -> {
            SampleLogging.configure(settings, "api");
            options.codecs().use(ZLinkMessagePackCodec.defaultCodec());
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(SampleLogging.flowLogPath(settings, "api-" + settings.apiHttpPort()))
                .traceLabel("api-" + settings.apiHttpPort());
            ClientServerChannelBuilder apiChannel = options.addClientServerChannel(SampleNames.ApiChannel)
                .enableServer(settings.apiChannelEndpoint());
            apiChannel.addRequestHandler(
                AuthenticatePlayerHandler.class,
                AuthenticatePlayerReq.class,
                AuthenticatePlayerRes.class);
            for (int index = 0; index < settings.playChannelEndpoints().size(); index++) {
                String endpoint = index == settings.playIndex()
                    ? settings.playChannelEndpoint()
                    : settings.playChannelEndpoints().get(index);
                options.addClientServerChannel(SampleNames.playChannel(index))
                    .enableClient(endpoint);
            }
        };
    }
}
