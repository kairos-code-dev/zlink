package systems.zlink.framework.configuration;

import systems.zlink.framework.streams.ZLinkSession;

public interface ZLinkStreamNodeBuilder {
    ZLinkStreamNodeBuilder bind(String endpoint);

    ZLinkStreamNodeBuilder attachActorGateway(String spotNodeName);

    ZLinkStreamNodeBuilder registerSession(Class<? extends ZLinkSession> sessionType);

    ZLinkStreamNodeBuilder addSessionPacketHandler(Class<?> handlerType);
}
