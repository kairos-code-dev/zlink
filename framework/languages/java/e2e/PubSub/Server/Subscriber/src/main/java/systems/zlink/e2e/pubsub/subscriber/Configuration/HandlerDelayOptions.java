package systems.zlink.e2e.pubsub.subscriber.Configuration;

public record HandlerDelayOptions(long delayMillis) {
    public static HandlerDelayOptions fromEnv() {
        String value = Env.get("ZLINK_JAVA_E2E_HANDLER_DELAY_MS");
        if (value.isBlank()) {
            return new HandlerDelayOptions(0);
        }
        return new HandlerDelayOptions(Long.parseLong(value));
    }
}
