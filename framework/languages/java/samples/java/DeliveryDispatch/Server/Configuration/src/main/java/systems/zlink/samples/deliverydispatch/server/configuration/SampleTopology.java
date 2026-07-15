package systems.zlink.samples.deliverydispatch.server.configuration;

import org.springframework.boot.context.properties.ConfigurationProperties;

@ConfigurationProperties("sample")
public record SampleTopology(
    String trackingChannelEndpoint,
    String trackingSpotEndpoint,
    String trackingSpotPubEndpoint,
    String customerStreamEndpoint,
    String courierStreamEndpoint,
    String dispatchHttpEndpoint,
    String dispatchChannelEndpoint,
    String customerSpotEndpoint,
    String customerSpotRouterEndpoint,
    String customerSpotNodeRid,
    String courierNodeRid,
    String courierActorNode1Rid,
    String courierActorNode2Rid,
    String courierActorNode1SpotEndpoint,
    String courierActorNode2SpotEndpoint,
    String courierActorNode1RouterEndpoint,
    String courierActorNode2RouterEndpoint,
    String courierSessionSpotRouterEndpoint,
    String courierSessionSpotEndpoint,
    String courierSessionSpotNodeRid,
    String redisEndpoint,
    String redisKeyPrefix,
    String courierNode,
    String logDirectory) {

    public SampleTopology {
        trackingChannelEndpoint = value(trackingChannelEndpoint, "tcp://127.0.0.1:48103");
        trackingSpotEndpoint = value(trackingSpotEndpoint, "tcp://127.0.0.1:48118");
        trackingSpotPubEndpoint = value(trackingSpotPubEndpoint, "tcp://127.0.0.1:48120");
        customerStreamEndpoint = value(customerStreamEndpoint, "tcp://127.0.0.1:48104");
        courierStreamEndpoint = value(courierStreamEndpoint, "tcp://127.0.0.1:48105");
        dispatchHttpEndpoint = value(dispatchHttpEndpoint, "http://127.0.0.1:48107");
        dispatchChannelEndpoint = value(dispatchChannelEndpoint, "tcp://127.0.0.1:48121");
        customerSpotEndpoint = value(customerSpotEndpoint, "tcp://127.0.0.1:48109");
        customerSpotRouterEndpoint = value(customerSpotRouterEndpoint, "tcp://127.0.0.1:48110");
        customerSpotNodeRid = value(customerSpotNodeRid, "customer-node-1");
        courierNodeRid = value(courierNodeRid, "courier-node-1");
        courierActorNode1Rid = value(courierActorNode1Rid, "courier-node-1");
        courierActorNode2Rid = value(courierActorNode2Rid, "courier-node-2");
        courierActorNode1SpotEndpoint = value(courierActorNode1SpotEndpoint, "tcp://127.0.0.1:48113");
        courierActorNode2SpotEndpoint = value(courierActorNode2SpotEndpoint, "tcp://127.0.0.1:48114");
        courierActorNode1RouterEndpoint = value(courierActorNode1RouterEndpoint, "tcp://127.0.0.1:48115");
        courierActorNode2RouterEndpoint = value(courierActorNode2RouterEndpoint, "tcp://127.0.0.1:48116");
        courierSessionSpotRouterEndpoint = value(courierSessionSpotRouterEndpoint, "tcp://127.0.0.1:48117");
        courierSessionSpotEndpoint = value(courierSessionSpotEndpoint, "tcp://127.0.0.1:48119");
        courierSessionSpotNodeRid = value(courierSessionSpotNodeRid, "courier-session-node");
        redisEndpoint = required(redisEndpoint, "redisEndpoint");
        redisKeyPrefix = value(redisKeyPrefix, "deliverydispatch:java:");
        courierNode = value(courierNode, "node1");
        logDirectory = required(logDirectory, "logDirectory");
    }

    public String courierPlacement(String courierId) {
        return "courier-b".equals(courierId) ? courierActorNode2Rid : courierActorNode1Rid;
    }

    public static String configPath(String[] args) {
        if (args.length != 2 || !"--config".equals(args[0]) || args[1].isBlank()) {
            throw new IllegalArgumentException("Usage: <role executable> --config <path>");
        }
        return args[1];
    }

    private static String value(String value, String fallback) {
        return value == null || value.isBlank() ? fallback : value;
    }

    private static String required(String value, String name) {
        if (value == null || value.isBlank()) {
            throw new IllegalArgumentException("sample." + name + " is required");
        }
        return value;
    }
}
