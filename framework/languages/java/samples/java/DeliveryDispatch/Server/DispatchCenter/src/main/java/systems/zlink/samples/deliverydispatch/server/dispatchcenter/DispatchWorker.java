package systems.zlink.samples.deliverydispatch.server.dispatchcenter;

import jakarta.annotation.PostConstruct;
import jakarta.annotation.PreDestroy;
import java.time.Duration;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.locks.LockSupport;
import org.springframework.stereotype.Component;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTimings;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages.Status;

@Component
public final class DispatchWorker {
    private final DispatchWorkQueue queue;
    private final ZLinkClient channels;
    private final AtomicBoolean running = new AtomicBoolean(false);
    private Thread worker;

    public DispatchWorker(DispatchWorkQueue queue, ZLinkClient channels) {
        this.queue = queue;
        this.channels = channels;
    }

    @PostConstruct
    public void start() {
        running.set(true);
        worker = new Thread(this::pump, "deliverydispatch-dispatch-worker");
        worker.setDaemon(true);
        worker.start();
    }

    @PreDestroy
    public void stop() {
        running.set(false);
        queue.signalStop();
        if (worker != null) {
            worker.interrupt();
        }
    }

    private void pump() {
        while (running.get()) {
            Messages.AssignDelivery request;
            try {
                request = queue.take();
            } catch (InterruptedException ignored) {
                Thread.currentThread().interrupt();
                return;
            }
            if (request == null) {
                return;
            }
            try {
                dispatch(request);
            } catch (RuntimeException error) {
                System.err.printf(
                    "deliverydispatch dispatch: failed delivery=%s error=%s%n",
                    request.deliveryId(),
                    error.getMessage());
            }
        }
    }

    private void dispatch(Messages.AssignDelivery request) {
        System.err.printf(
            "deliverydispatch dispatch: assign delivery=%s customer=%s%n",
            request.deliveryId(),
            request.customerId());
        publishStatus(request.deliveryId(), Status.Assigned, SampleNames.CourierA);

        Messages.OfferDeliveryResult first = tryOffer(request, SampleNames.CourierA);
        if (first.accepted()) {
            continueAccepted(request.deliveryId(), first.courierId());
            return;
        }

        publishStatus(request.deliveryId(), Status.Reassigned, SampleNames.CourierB);
        Messages.OfferDeliveryResult second = requestWithRetry(
            SampleNames.courierChannel(SampleNames.CourierB),
            new Messages.OfferDelivery(
                request.deliveryId(),
                request.pickupAddress(),
                request.dropoffAddress()),
            Messages.OfferDeliveryResult.class,
            null,
            SampleTimings.MaxChannelAttempts);
        if (!second.accepted()) {
            publishStatus(request.deliveryId(), Status.Failed, second.courierId());
            throw new IllegalStateException(
                "Delivery '" + request.deliveryId() + "' was rejected by all couriers.");
        }
        continueAccepted(request.deliveryId(), second.courierId());
    }

    private Messages.OfferDeliveryResult tryOffer(
        Messages.AssignDelivery request,
        String courierId) {
        try {
            return requestWithRetry(
                SampleNames.courierChannel(courierId),
                new Messages.OfferDelivery(
                    request.deliveryId(),
                    request.pickupAddress(),
                    request.dropoffAddress()),
                Messages.OfferDeliveryResult.class,
                SampleTimings.DispatchTimeout,
                1);
        } catch (RuntimeException error) {
            System.err.printf(
                "deliverydispatch dispatch: courier timeout delivery=%s courier=%s%n",
                request.deliveryId(),
                courierId);
            return new Messages.OfferDeliveryResult(
                request.deliveryId(),
                courierId,
                false,
                error.getMessage());
        }
    }

    private void continueAccepted(String deliveryId, String courierId) {
        publishStatus(deliveryId, Status.Accepted, courierId);
        publishStatus(deliveryId, Status.PickedUp, courierId);
        publishStatus(deliveryId, Status.Delivered, courierId);
    }

    private void publishStatus(String deliveryId, String status, String courierId) {
        requestWithRetry(
            SampleNames.TrackingChannel,
            new Messages.DeliveryStatusChanged(
                deliveryId,
                status,
                courierId,
                System.currentTimeMillis()),
            Messages.DeliveryStatusAck.class,
            null,
            SampleTimings.MaxChannelAttempts);
    }

    private <TReply> TReply requestWithRetry(
        String channelName,
        Object request,
        Class<TReply> replyType,
        Duration timeout,
        int maxAttempts) {
        RuntimeException lastError = null;
        for (int attempt = 1; attempt <= maxAttempts; attempt++) {
            try {
                var call = channels.requestToChannel(channelName, request);
                if (timeout != null) {
                    call = call.timeout(timeout);
                } else {
                    call = call.timeout(SampleTimings.RequestTimeout);
                }
                return call.await(replyType);
            } catch (RuntimeException error) {
                lastError = error;
                if (maxAttempts == 1) {
                    throw error;
                }
                LockSupport.parkNanos(SampleTimings.ChannelRetryDelay.toNanos());
            }
        }
        throw new IllegalStateException(
            "Channel '" + channelName + "' was not ready for request.",
            lastError);
    }
}
