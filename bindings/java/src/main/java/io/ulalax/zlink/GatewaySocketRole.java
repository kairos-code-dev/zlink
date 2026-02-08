package io.ulalax.zlink;

public enum GatewaySocketRole {
    ROUTER(1);

    private final int value;
    GatewaySocketRole(int v) { this.value = v; }
    public int getValue() { return value; }
}
