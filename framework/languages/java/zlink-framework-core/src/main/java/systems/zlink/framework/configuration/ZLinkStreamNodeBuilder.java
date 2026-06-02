package systems.zlink.framework.configuration;

import systems.zlink.framework.streams.ZLinkSession;

public interface ZLinkStreamNodeBuilder {
    void bind(String endpoint);

    void attachActorGateway(String spotNodeName);

    void registerSession(Class<? extends ZLinkSession> sessionType);
}
