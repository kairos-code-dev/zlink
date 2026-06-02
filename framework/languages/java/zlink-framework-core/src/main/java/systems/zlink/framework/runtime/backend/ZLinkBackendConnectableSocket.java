package systems.zlink.framework.runtime.backend;

public interface ZLinkBackendConnectableSocket extends ZLinkBackendSocket {
    void connect(String endpoint);

    void disconnect(String endpoint);
}
