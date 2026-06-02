package systems.zlink.framework.runtime;

public interface ZLinkBackendObject extends AutoCloseable {
    String name();

    @Override
    void close();
}
