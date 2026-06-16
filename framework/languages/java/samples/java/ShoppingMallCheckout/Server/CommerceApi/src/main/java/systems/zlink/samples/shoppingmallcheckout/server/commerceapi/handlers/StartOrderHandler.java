package systems.zlink.samples.shoppingmallcheckout.server.commerceapi.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.shoppingmallcheckout.server.commerceapi.StartOrderUseCase;
import systems.zlink.samples.shoppingmallcheckout.shared.contracts.Messages;

@ZLinkHandlerGroup("commerce")
public final class StartOrderHandler
    implements ZLinkRequestHandler<Messages.StartOrderReq, Messages.StartOrderRes> {
    private final StartOrderUseCase useCase;

    public StartOrderHandler(StartOrderUseCase useCase) {
        this.useCase = useCase;
    }

    @Override
    public Messages.StartOrderRes handle(
        Messages.StartOrderReq request,
        ZLinkRequestContext context) {
        return useCase.execute(request);
    }
}
