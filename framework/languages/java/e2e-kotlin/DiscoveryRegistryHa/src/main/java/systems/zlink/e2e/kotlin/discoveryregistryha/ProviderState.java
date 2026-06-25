package systems.zlink.e2e.kotlin.discoveryregistryha;

public final class ProviderState {
    private final String providerRid;

    public ProviderState(String providerRid) {
        this.providerRid = providerRid;
    }

    public String providerRid() {
        return providerRid;
    }
}
