package systems.zlink.framework.runtime;

public interface ZLinkBackendConnectableSocket extends ZLinkBackendSocket {
    void connect(String endpoint);

    void disconnect(String endpoint);
}
