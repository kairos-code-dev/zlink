package systems.zlink.samples.deliverydispatch.server.couriergateway;

import systems.zlink.framework.actors.ZLinkActorRefSnapshot;

public record CourierBinding(
    String courierId,
    ZLinkActorRefSnapshot actor,
    String sessionRoute) {
}
