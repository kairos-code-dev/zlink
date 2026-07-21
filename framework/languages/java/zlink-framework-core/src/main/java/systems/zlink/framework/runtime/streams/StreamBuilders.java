package systems.zlink.framework.runtime.streams;

import systems.zlink.framework.configuration.ZLinkStreamNodeBuilder;
import systems.zlink.framework.streams.ZLinkSession;

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
        public ZLinkStreamNodeBuilder setTlsServer(String certificatePath, String keyPath) {
            registration.setTlsServer(certificatePath, keyPath, false);
            return this;
        }

        @Override
        public ZLinkStreamNodeBuilder setTlsServer(
            String certificatePath,
            String keyPath,
            boolean requireClientCertificate) {
            registration.setTlsServer(certificatePath, keyPath, requireClientCertificate);
            return this;
        }

        @Override
        public ZLinkStreamNodeBuilder registerSession(Class<? extends ZLinkSession> sessionType) {
            registration.registerSession(sessionType);
            return this;
        }

        @Override
        public ZLinkStreamNodeBuilder enableActorDispatch(String meshName) {
            registration.enableActorDispatch(meshName);
            return this;
        }

        @Override
        public ZLinkStreamNodeBuilder addSessionPacketHandler(Class<?> handlerType) {
            registration.addSessionPacketHandler(handlerType);
            return this;
        }
    }
}
