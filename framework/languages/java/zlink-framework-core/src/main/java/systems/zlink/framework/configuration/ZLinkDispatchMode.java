package systems.zlink.framework.configuration;

public enum ZLinkDispatchMode {
    COMPILED(0),
    DYNAMIC(1);

    private final int value;

    ZLinkDispatchMode(int value) { this.value = value; }

    public int value() { return value; }
}
