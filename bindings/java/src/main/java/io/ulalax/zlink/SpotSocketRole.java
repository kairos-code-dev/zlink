package io.ulalax.zlink;

public enum SpotSocketRole {
    PUB(1), SUB(2);

    private final int value;
    SpotSocketRole(int v) { this.value = v; }
    public int getValue() { return value; }
}
