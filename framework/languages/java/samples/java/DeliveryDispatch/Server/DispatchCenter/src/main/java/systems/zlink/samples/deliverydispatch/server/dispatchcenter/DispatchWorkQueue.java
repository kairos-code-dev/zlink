package systems.zlink.samples.deliverydispatch.server.dispatchcenter;

import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

public final class DispatchWorkQueue {
    private static final Messages.AssignDelivery POISON =
        new Messages.AssignDelivery("", "", "", "");

    private final BlockingQueue<Messages.AssignDelivery> queue = new LinkedBlockingQueue<>();

    public void enqueue(Messages.AssignDelivery delivery) {
        queue.add(delivery);
    }

    public Messages.AssignDelivery take() throws InterruptedException {
        Messages.AssignDelivery delivery = queue.take();
        return delivery == POISON ? null : delivery;
    }

    public void signalStop() {
        queue.add(POISON);
    }
}
