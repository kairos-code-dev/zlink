package systems.zlink.framework.locations;

public enum ZLinkLocationChangeType {
    UPSERTED(1),
    REMOVED(2),
    EXPIRED(3);

    private final int value;

    ZLinkLocationChangeType(int value) {
        this.value = value;
    }

    public int value() {
        return value;
    }
}
