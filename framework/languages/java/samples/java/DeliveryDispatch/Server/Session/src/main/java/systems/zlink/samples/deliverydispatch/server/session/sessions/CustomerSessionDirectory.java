package systems.zlink.samples.deliverydispatch.server.session.sessions;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

public final class CustomerSessionDirectory {
    private final Object gate = new Object();
    private final Map<String, ZLinkSessionContext> sessions = new HashMap<>();
    private final Map<String, Set<String>> subscriptions = new HashMap<>();

    public void add(ZLinkSessionContext context) {
        synchronized (gate) {
            sessions.put(context.sessionId(), context);
        }
    }

    public void remove(ZLinkSessionContext context) {
        synchronized (gate) {
            sessions.remove(context.sessionId());
            for (Set<String> subscribers : subscriptions.values()) {
                subscribers.remove(context.sessionId());
            }
        }
    }

    public void subscribe(ZLinkSessionContext context, String deliveryId) {
        synchronized (gate) {
            sessions.put(context.sessionId(), context);
            subscriptions
                .computeIfAbsent(deliveryId, key -> new HashSet<>())
                .add(context.sessionId());
        }
    }

    public void publish(Messages.DeliveryStatusNotify status) {
        List<ZLinkSessionContext> targets = new ArrayList<>();
        synchronized (gate) {
            Set<String> subscribers = subscriptions.get(status.deliveryId());
            if (subscribers == null) {
                return;
            }
            for (String sessionId : subscribers) {
                ZLinkSessionContext context = sessions.get(sessionId);
                if (context != null) {
                    targets.add(context);
                }
            }
        }
        for (ZLinkSessionContext target : targets) {
            target.client().send(status).await();
        }
    }
}
