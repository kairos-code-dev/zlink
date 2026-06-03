package systems.zlink.samples.tictactoe.server.api.handlers;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkRequest;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.shared.contracts.CreateGameRes;

@ZLinkHandlerGroup("api")
public final class CreateGameHttpHandler {
    private final ZLinkClient client;

    public CreateGameHttpHandler(ZLinkClient client) {
        this.client = client;
    }

    @ZLinkRequest(packetName = "CreateGame")
    public CompletionStage<CreateGameRes> handleAsync(String request) {
        return client.requestToChannel(SampleNames.PlayChannel, request)
            .packetName("CreateGameReq")
            .submitAsync(CreateGameRes.class);
    }
}
