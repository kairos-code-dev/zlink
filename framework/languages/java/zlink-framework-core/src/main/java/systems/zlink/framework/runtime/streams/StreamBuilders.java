package systems.zlink.framework.runtime.streams;

import systems.zlink.framework.configuration.ZLinkStreamNodeBuilder;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionPacketHandler;

public final class StreamBuilders {
    private StreamBuilders() {
    }

    public static ZLinkStreamNodeBuilder streamNode(StreamNodeRegistration registration) {
        return new StreamNode(registration);
    }

    private record StreamNode(StreamNodeRegistration registration)
        implements ZLinkStreamNodeBuilder {
        @Override
        public void bind(String endpoint) {
            registration.bind(endpoint);
        }

        @Override
        public void attachActorGateway(String spotNodeName) {
            registration.attachActorGateway(spotNodeName);
        }

        @Override
        public void registerSession(Class<? extends ZLinkSession> sessionType) {
            registration.registerSession(sessionType);
        }

        @Override
        public void addSessionPacketHandler(
            Class<? extends ZLinkSessionPacketHandler<?>> handlerType) {
            registration.addSessionPacketHandler(handlerType);
        }
    }
}
