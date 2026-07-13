package systems.zlink.samples.deliverydispatch.server.couriergateway;

import java.util.HashMap;
import java.util.Map;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTopology;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

public final class CourierDirectory {
    private final Object gate = new Object();
    private final Map<String, CourierBinding> actors = new HashMap<>();

    public String choosePlacement(String courierId) {
        return SampleTopology.courierPlacement(courierId);
    }

    public CourierBinding remember(Messages.EnsureCourierActorRes ensured, String sessionRoute) {
        CourierBinding binding = new CourierBinding(ensured.courierId(), ensured.actor(), sessionRoute);
        synchronized (gate) {
            actors.put(ensured.courierId(), binding);
        }
        return binding;
    }

    public CourierBinding require(String courierId) {
        synchronized (gate) {
            CourierBinding binding = actors.get(courierId);
            if (binding != null) {
                return binding;
            }
        }
        throw new IllegalStateException("Courier actor is not bound: " + courierId);
    }
}
