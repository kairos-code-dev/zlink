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
        public ZLinkStreamNodeBuilder bind(String endpoint) {
            registration.bind(endpoint);
            return this;
        }

        @Override
        public ZLinkStreamNodeBuilder attachActorGateway(String spotNodeName) {
            registration.attachActorGateway(spotNodeName);
            return this;
        }

        @Override
        public ZLinkStreamNodeBuilder registerSession(Class<? extends ZLinkSession> sessionType) {
            registration.registerSession(sessionType);
            return this;
        }

        @Override
        public ZLinkStreamNodeBuilder addSessionPacketHandler(
            Class<? extends ZLinkSessionPacketHandler<?>> handlerType) {
            registration.addSessionPacketHandler(handlerType);
            return this;
        }
    }
}
