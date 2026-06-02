package systems.zlink.samples.tictactoe.server.api.handlers;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.samples.tictactoe.server.play.handlers.CreateGameHandler;

public final class CreateGameHttpHandler implements ZLinkRequestHandler<String, String> {
    private final CreateGameHandler playHandler = new CreateGameHandler();

    @Override
    public CompletionStage<String> handleAsync(String request, ZLinkRequestContext context) {
        return playHandler.createAsync(request);
    }
}
