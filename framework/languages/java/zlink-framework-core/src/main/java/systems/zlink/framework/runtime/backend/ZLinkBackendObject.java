package systems.zlink.framework.runtime.backend;

public interface ZLinkBackendObject extends AutoCloseable {
    String name();

    @Override
    void close();
}
