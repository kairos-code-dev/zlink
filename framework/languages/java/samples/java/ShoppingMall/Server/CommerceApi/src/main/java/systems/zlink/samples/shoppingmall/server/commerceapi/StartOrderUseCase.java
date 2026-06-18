package systems.zlink.samples.shoppingmall.server.commerceapi;

import java.util.Optional;
import java.util.concurrent.locks.LockSupport;
import org.springframework.stereotype.Component;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.samples.shoppingmall.server.configuration.CommerceStore;
import systems.zlink.samples.shoppingmall.server.configuration.CommerceStore.IdempotencyMapping;
import systems.zlink.samples.shoppingmall.server.configuration.SampleNames;
import systems.zlink.samples.shoppingmall.server.configuration.SampleTimings;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages.CartSeed;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages.OrderState;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages.StartOrderReq;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages.StartOrderRes;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages.StartOrderWorkflowReq;

/**
 * Order-start use case: idempotency lookup, cross-instance forwarding for
 * pending mappings owned elsewhere, input validation, and workflow relay.
 * Never appends domain events itself.
 */
@Component
public final class StartOrderUseCase {
    private final CommerceStore store;
    private final OrderWorkflowRouter workflows;
    private final ZLinkClient channels;
    private final CommerceApiInstanceOptions options;

    public StartOrderUseCase(
        CommerceStore store,
        OrderWorkflowRouter workflows,
        ZLinkClient channels,
        CommerceApiInstanceOptions options) {
        this.store = store;
        this.workflows = workflows;
        this.channels = channels;
        this.options = options;
    }

    public StartOrderRes execute(StartOrderReq request) {
        Optional<IdempotencyMapping> existing = store.findIdempotency(request.idempotencyKey());
        if (existing.isPresent() && existing.get().started()) {
            OrderState state = store.findReadModel(existing.get().orderId())
                .orElseGet(() -> store.placeholder(existing.get().orderId()));
            return new StartOrderRes(state.orderId(), state.status());
        }
        if (existing.isPresent()
            && !existing.get().ownerInstanceId().equals(options.instanceId())) {
            return forwardToOwner(existing.get().ownerInstanceId(), request);
        }

        CartSeed cart = store.findCart(request.cartId())
            .orElseThrow(() -> new IllegalArgumentException("Unknown cart '" + request.cartId() + "'."));
        if (!store.shippingAddressExists(request.shippingAddressId())) {
            throw new IllegalArgumentException("Unknown shipping address '" + request.shippingAddressId() + "'.");
        }
        if (!store.paymentMethodExists(request.paymentMethodId())) {
            throw new IllegalArgumentException("Unknown payment method '" + request.paymentMethodId() + "'.");
        }

        IdempotencyMapping mapping = existing.orElseGet(
            () -> store.reserveIdempotency(request.idempotencyKey(), options.instanceId()));
        store.saveOrderPaymentMethod(mapping.orderId(), request.paymentMethodId());

        StartOrderWorkflowReq command = new StartOrderWorkflowReq(
            mapping.orderId(),
            request.cartId(),
            request.shippingAddressId(),
            request.paymentMethodId(),
            request.idempotencyKey(),
            cart.lines(),
            cart.amount(),
            cart.currency());
        OrderState state = workflows.startWorkflow(command);
        return new StartOrderRes(state.orderId(), state.status());
    }

    private StartOrderRes forwardToOwner(String ownerInstanceId, StartOrderReq request) {
        String channel = SampleNames.commerceApiChannel(ownerInstanceId);
        RuntimeException lastError = null;
        for (int attempt = 1; attempt <= SampleTimings.MaxChannelAttempts; attempt++) {
            try {
                return channels.requestToChannel(channel, request)
                    .timeout(SampleTimings.RequestTimeout)
                    .await(StartOrderRes.class);
            } catch (RuntimeException error) {
                lastError = error;
                LockSupport.parkNanos(SampleTimings.ChannelRetryDelay.toNanos());
            }
        }
        throw new IllegalStateException(
            "Peer CommerceApi '" + ownerInstanceId + "' was not ready for forwarded start.", lastError);
    }
}
