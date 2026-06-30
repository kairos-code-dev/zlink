package systems.zlink.e2e.pubsub.subscriber.Configuration;

import java.util.HashSet;
import java.util.Set;

public record SubscriberOptions(
    String rid,
    Set<String> topics,
    String httpEndpoint,
    String registryRouterEndpoint,
    String logDir,
    HandlerDelayOptions delay) {
    public static SubscriberOptions fromEnv() {
        return new SubscriberOptions(
            Env.get("ZLINK_JAVA_E2E_SUBSCRIBER_RID", "sub-1"),
            topicsFromEnv(),
            Env.get("ZLINK_JAVA_E2E_HTTP_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_REGISTRY_ROUTER"),
            Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs"),
            HandlerDelayOptions.fromEnv());
    }

    private static Set<String> topicsFromEnv() {
        Set<String> values = new HashSet<>();
        for (String part : Env.get("ZLINK_JAVA_E2E_TOPICS", "all").split(",")) {
            String trimmed = part.trim();
            if (!trimmed.isEmpty()) {
                values.add(trimmed);
            }
        }
        values.add("all");
        return Set.copyOf(values);
    }
}
