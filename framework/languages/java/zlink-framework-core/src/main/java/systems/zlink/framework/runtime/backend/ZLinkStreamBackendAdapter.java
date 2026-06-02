package systems.zlink.framework.runtime.backend;

public interface ZLinkStreamBackendAdapter {
    ZLinkBackendStreamSocket createStreamSocket(ZLinkBackendContext context);
}
