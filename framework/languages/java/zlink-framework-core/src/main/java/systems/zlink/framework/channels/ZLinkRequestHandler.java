package systems.zlink.framework.channels;

public interface ZLinkRequestHandler<TRequest, TReply> {
    TReply handle(
        TRequest request,
        ZLinkRequestContext context);
}
