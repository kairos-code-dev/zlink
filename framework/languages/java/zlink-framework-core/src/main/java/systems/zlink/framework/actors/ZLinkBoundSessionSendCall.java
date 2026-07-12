package systems.zlink.framework.actors;


public interface ZLinkBoundSessionSendCall {
    ZLinkBoundSessionSendCall metadata(String key, String value);

    void submit();
}
