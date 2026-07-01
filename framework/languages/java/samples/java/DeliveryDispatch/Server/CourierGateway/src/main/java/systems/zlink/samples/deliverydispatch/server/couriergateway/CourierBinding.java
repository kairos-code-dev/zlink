package systems.zlink.samples.deliverydispatch.server.couriergateway;

import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

public record CourierBinding(
    String courierId,
    Messages.ActorRefSnapshot actor,
    String sessionRoute) {
}
