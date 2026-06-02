package systems.zlink.framework.configuration;

import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionPacketHandler;

public interface ZLinkStreamNodeBuilder {
    void bind(String endpoint);

    void attachActorGateway(String spotNodeName);

    void registerSession(Class<? extends ZLinkSession> sessionType);

    void addSessionPacketHandler(
        Class<? extends ZLinkSessionPacketHandler<?>> handlerType);
}
