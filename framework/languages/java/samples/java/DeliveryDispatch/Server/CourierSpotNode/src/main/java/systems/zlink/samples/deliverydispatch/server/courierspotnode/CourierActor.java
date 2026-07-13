package systems.zlink.samples.deliverydispatch.server.courierspotnode;

import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTimings;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

public final class CourierActor implements ZLinkActor {
    private final String actorId;
    private final ZLinkActorContext context;
    private final Object gate = new Object();
    private final Map<String, CompletableFuture<Messages.CourierDecision>> pending = new HashMap<>();

    public CourierActor(String actorId, ZLinkActorContext context) {
        this.actorId = actorId;
        this.context = context;
    }

    @Override
    public String actorId() {
        return actorId;
    }

    @Override
    public ZLinkActorContext context() {
        return context;
    }

    public Messages.OfferDeliveryRes offer(Messages.OfferDeliveryReq offer) {
        CompletableFuture<Messages.CourierDecision> decision = new CompletableFuture<>();
        synchronized (gate) {
            pending.put(offer.deliveryId(), decision);
        }
        try {
            context.boundSession()
                .send(new Messages.OfferDeliveryNotify(
                    offer.courierId(),
                    offer.deliveryId(),
                    offer.pickupAddress(),
                    offer.dropoffAddress()))
                .submit();
            Messages.CourierDecision accepted =
                decision.get(SampleTimings.CourierDecisionTimeout.toMillis(), TimeUnit.MILLISECONDS);
            return new Messages.OfferDeliveryRes(
                offer.deliveryId(),
                offer.courierId(),
                accepted.accepted(),
                accepted.reason());
        } catch (java.util.concurrent.TimeoutException ex) {
            return new Messages.OfferDeliveryRes(
                offer.deliveryId(),
                offer.courierId(),
                false,
                "courier did not respond before dispatch timeout");
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("courier offer was interrupted", ex);
        } catch (java.util.concurrent.ExecutionException ex) {
            throw new IllegalStateException("courier decision failed", ex);
        } finally {
            synchronized (gate) {
                pending.remove(offer.deliveryId());
            }
        }
    }

    public void complete(Messages.CourierDecision decision) {
        CompletableFuture<Messages.CourierDecision> waiting;
        synchronized (gate) {
            waiting = pending.get(decision.deliveryId());
        }
        if (waiting != null) {
            waiting.complete(decision);
        }
    }
}
