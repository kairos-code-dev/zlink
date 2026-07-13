package systems.zlink.samples.deliverydispatch.server.customergateway.handlers;

import systems.zlink.framework.actors.ActorRefSnapshot;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;
import java.util.concurrent.CompletionStage;

@ZLinkHandlerGroup("customer-route")
public final class EnsureCustomerActorHandler
    implements ZLinkRequestHandler<Messages.EnsureCustomerActorReq, Messages.EnsureCustomerActorRes> {
    private final ZLinkActorManager actors;

    public EnsureCustomerActorHandler(ZLinkActorManager actors) {
        this.actors = actors;
    }

    @Override
    public CompletionStage<Messages.EnsureCustomerActorRes> handle(
        Messages.EnsureCustomerActorReq request,
        ZLinkRequestContext context) {
        return actors.getOrCreate(request.customerId(), SampleNames.CustomerActorType, request)
            .thenApply(actor -> new Messages.EnsureCustomerActorRes(
                request.customerId(), ActorRefSnapshot.from(actor)));
    }
}
