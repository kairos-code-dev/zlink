package systems.zlink.framework.runtime.configuration;

import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.Set;
import systems.zlink.framework.configuration.ZLinkCodecRegistryBuilder;

public final class ZLinkCodecRegistration implements ZLinkCodecRegistryBuilder {
    private final Set<String> registeredCodecs = new LinkedHashSet<>();

    @Override
    public void addJson() {
        registeredCodecs.add("json");
    }

    @Override
    public void addMessagePack() {
        registeredCodecs.add("messagepack");
    }

    @Override
    public void addProtobuf() {
        registeredCodecs.add("protobuf");
    }

    public Set<String> registeredCodecs() {
        return Collections.unmodifiableSet(registeredCodecs);
    }
}
