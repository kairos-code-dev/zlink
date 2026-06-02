package systems.zlink.framework.runtime;

public interface ZLinkStreamBackendAdapter {
    ZLinkBackendStreamSocket createStreamSocket(ZLinkBackendContext context);
}
