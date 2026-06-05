package systems.zlink.framework.runtime.configuration;

import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.Set;
import systems.zlink.framework.configuration.ZLinkMetadataPolicyBuilder;
import systems.zlink.framework.errors.ZLinkConfigurationException;

public final class ZLinkMetadataPolicyRegistration implements ZLinkMetadataPolicyBuilder {
    private final Set<String> forwardedApplicationKeys = new LinkedHashSet<>();

    @Override
    public void addForwardedMetadataKey(String key) {
        if (key == null || key.isBlank()) {
            throw new ZLinkConfigurationException("metadata key is required");
        }
        forwardedApplicationKeys.add(key);
    }

    public Set<String> forwardedApplicationKeys() {
        return Collections.unmodifiableSet(forwardedApplicationKeys);
    }
}
