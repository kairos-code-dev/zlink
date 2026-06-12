package systems.zlink.framework.channels;

public interface ZLinkRouteRequestHandler<TRequest, TReply> {
    TReply handle(
        TRequest request,
        ZLinkRouteRequestContext context);
}
