package systems.zlink.framework.configuration;

import systems.zlink.framework.streams.ZLinkSession;

public interface ZLinkStreamNodeBuilder {
    ZLinkStreamNodeBuilder bind(String endpoint);

    ZLinkStreamNodeBuilder registerSession(Class<? extends ZLinkSession> sessionType);

    ZLinkStreamNodeBuilder addSessionPacketHandler(Class<?> handlerType);
}
