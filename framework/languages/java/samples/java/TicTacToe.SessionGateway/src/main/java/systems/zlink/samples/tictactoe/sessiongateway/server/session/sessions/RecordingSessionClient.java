package systems.zlink.samples.tictactoe.sessiongateway.server.session.sessions;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.streams.ZLinkSessionClient;
import systems.zlink.framework.streams.ZLinkSessionReplyCall;
import systems.zlink.framework.streams.ZLinkSessionSendCall;

public final class RecordingSessionClient implements ZLinkSessionClient {
    @Override
    public <TMessage> ZLinkSessionSendCall send(TMessage message) {
        return new RecordingSessionSendCall();
    }

    @Override
    public <TMessage> ZLinkSessionReplyCall reply(TMessage message) {
        return new RecordingSessionReplyCall();
    }

    private static final class RecordingSessionSendCall implements ZLinkSessionSendCall {
        @Override
        public ZLinkSessionSendCall metadata(String key, String value) {
            return this;
        }

        @Override
        public ZLinkSessionSendCall packetName(String messageName) {
            return this;
        }

        @Override
        public ZLinkSessionSendCall compress() {
            return this;
        }

        @Override
        public CompletionStage<Void> submitAsync() {
            return CompletableFuture.completedFuture(null);
        }
    }

    private static final class RecordingSessionReplyCall implements ZLinkSessionReplyCall {
        @Override
        public ZLinkSessionReplyCall metadata(String key, String value) {
            return this;
        }

        @Override
        public ZLinkSessionReplyCall compress() {
            return this;
        }

        @Override
        public CompletionStage<Void> submitAsync() {
            return CompletableFuture.completedFuture(null);
        }
    }
}
