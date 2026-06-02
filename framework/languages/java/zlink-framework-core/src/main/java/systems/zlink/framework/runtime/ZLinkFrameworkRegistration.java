package systems.zlink.framework.runtime;

import java.time.Duration;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import systems.zlink.framework.actors.ZLinkActorFactory;

final class ZLinkFrameworkRegistration {
    private final List<String> registryEndpoints = new ArrayList<>();
    private final List<ChannelRegistration> channels = new ArrayList<>();
    private final List<SpotNodeRegistration> spotNodes = new ArrayList<>();
    private final List<StreamNodeRegistration> streamNodes = new ArrayList<>();
    private final Map<String, Class<? extends ZLinkActorFactory>> actorFactories =
        new LinkedHashMap<>();
    private Duration defaultTimeout = Duration.ofSeconds(30);

    Duration defaultTimeout() {
        return defaultTimeout;
    }

    void setDefaultTimeout(Duration defaultTimeout) {
        this.defaultTimeout = defaultTimeout;
    }

    List<String> registryEndpoints() {
        return registryEndpoints;
    }

    List<ChannelRegistration> channels() {
        return channels;
    }

    List<SpotNodeRegistration> spotNodes() {
        return spotNodes;
    }

    List<StreamNodeRegistration> streamNodes() {
        return streamNodes;
    }

    Map<String, Class<? extends ZLinkActorFactory>> actorFactories() {
        return actorFactories;
    }

    boolean discoveryEnabled() {
        return !registryEndpoints.isEmpty();
    }

    void validate() {
        for (ChannelRegistration channel : channels) {
            channel.validate(discoveryEnabled());
        }
        for (SpotNodeRegistration spotNode : spotNodes) {
            spotNode.validate();
        }
        for (StreamNodeRegistration streamNode : streamNodes) {
            streamNode.validate(spotNodes);
        }
    }
}
