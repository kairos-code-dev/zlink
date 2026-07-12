package systems.zlink.framework.channels;

public interface ZLinkPublishCall {
    ZLinkPublishCall metadata(String key, String value);

    void submit();
}
