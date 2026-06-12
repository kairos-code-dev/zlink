package systems.zlink.framework.monitoring;

public interface ZLinkRuntimeEventHandler<TEvent extends ZLinkRuntimeEvent> {
    void handle(TEvent event);
}
