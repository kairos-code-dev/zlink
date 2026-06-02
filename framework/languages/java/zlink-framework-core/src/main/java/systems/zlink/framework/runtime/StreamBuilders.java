package systems.zlink.framework.runtime;

import systems.zlink.framework.configuration.ZLinkStreamNodeBuilder;
import systems.zlink.framework.streams.ZLinkSession;

final class StreamBuilders {
    private StreamBuilders() {
    }

    static ZLinkStreamNodeBuilder streamNode(StreamNodeRegistration registration) {
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
    }
}
