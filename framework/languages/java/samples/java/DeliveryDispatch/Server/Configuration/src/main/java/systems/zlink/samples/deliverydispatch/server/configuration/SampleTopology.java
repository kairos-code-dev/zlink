package systems.zlink.samples.deliverydispatch.server.configuration;

public final class SampleTopology {
    public static final String TrackingChannelEndpoint =
        property("trackingChannelEndpoint", "tcp://127.0.0.1:48103");
    public static final String TrackingSpotEndpoint =
        property("trackingSpotEndpoint", "tcp://127.0.0.1:48118");
    public static final String TrackingSpotPubEndpoint =
        property("trackingSpotPubEndpoint", "tcp://127.0.0.1:48120");
    public static final String CustomerStreamEndpoint =
        property("customerStreamEndpoint", "tcp://127.0.0.1:48104");
    public static final String CourierStreamEndpoint =
        property("courierStreamEndpoint", "tcp://127.0.0.1:48105");
    public static final String DispatchHttpEndpoint =
        property("dispatchHttpEndpoint", "http://127.0.0.1:48107");
    public static final String DispatchChannelEndpoint =
        property("dispatchChannelEndpoint", "tcp://127.0.0.1:48121");
    public static final String CustomerSpotEndpoint =
        property("customerSpotEndpoint", "tcp://127.0.0.1:48109");
    public static final String CustomerSpotRouterEndpoint =
        property("customerSpotRouterEndpoint", "tcp://127.0.0.1:48110");
    public static final String CustomerSpotNodeRid =
        property("customerSpotNodeRid", "customer-node-1");
    public static final String CourierNodeRid =
        property("courierNodeRid", "courier-node-1");
    public static final String CourierActorNode1Rid =
        property("courierActorNode1Rid", "courier-node-1");
    public static final String CourierActorNode2Rid =
        property("courierActorNode2Rid", "courier-node-2");
    public static final String CourierActorNode1SpotEndpoint =
        property("courierActorNode1SpotEndpoint", "tcp://127.0.0.1:48113");
    public static final String CourierActorNode2SpotEndpoint =
        property("courierActorNode2SpotEndpoint", "tcp://127.0.0.1:48114");
    public static final String CourierActorNode1RouterEndpoint =
        property("courierActorNode1RouterEndpoint", "tcp://127.0.0.1:48115");
    public static final String CourierActorNode2RouterEndpoint =
        property("courierActorNode2RouterEndpoint", "tcp://127.0.0.1:48116");
    public static final String CourierSessionSpotRouterEndpoint =
        property("courierSessionSpotRouterEndpoint", "tcp://127.0.0.1:48117");
    public static final String CourierSessionSpotEndpoint =
        property("courierSessionSpotEndpoint", "tcp://127.0.0.1:48119");
    public static final String CourierSessionSpotNodeRid =
        property("courierSessionSpotNodeRid", "courier-session-node");
    public static final String RedisEndpoint = requiredProperty("redisEndpoint");
    public static final String RedisKeyPrefix =
        property("redisKeyPrefix", "deliverydispatch:java:");

    private SampleTopology() {
    }

    private static String property(String name, String fallback) {
        return System.getProperty("zlink.samples.deliverydispatch." + name, fallback);
    }

    private static String requiredProperty(String name) {
        String value = System.getProperty("zlink.samples.deliverydispatch." + name);
        if (value == null || value.isBlank()) {
            throw new IllegalStateException("Missing zlink.samples.deliverydispatch." + name);
        }
        return value;
    }

    public static String courierPlacement(String courierId) {
        return "courier-b".equals(courierId) ? CourierActorNode2Rid : CourierActorNode1Rid;
    }
}
