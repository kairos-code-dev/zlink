package systems.zlink.samples.deliverydispatch.server.configuration;

import java.io.Reader;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Properties;

public final class SampleTopology {
    public static String TrackingChannelEndpoint;
    public static String TrackingSpotEndpoint;
    public static String TrackingSpotPubEndpoint;
    public static String CustomerStreamEndpoint;
    public static String CourierStreamEndpoint;
    public static String DispatchHttpEndpoint;
    public static String DispatchChannelEndpoint;
    public static String CustomerSpotEndpoint;
    public static String CustomerSpotRouterEndpoint;
    public static String CustomerSpotNodeRid;
    public static String CourierNodeRid;
    public static String CourierActorNode1Rid;
    public static String CourierActorNode2Rid;
    public static String CourierActorNode1SpotEndpoint;
    public static String CourierActorNode2SpotEndpoint;
    public static String CourierActorNode1RouterEndpoint;
    public static String CourierActorNode2RouterEndpoint;
    public static String CourierSessionSpotRouterEndpoint;
    public static String CourierSessionSpotEndpoint;
    public static String CourierSessionSpotNodeRid;
    public static String RedisEndpoint;
    public static String RedisKeyPrefix;
    public static String CourierNode;
    public static String LogDirectory;

    private SampleTopology() {
    }

    public static void configure(String[] args) {
        Properties properties = load(args);
        TrackingChannelEndpoint = value(properties, "trackingChannelEndpoint", "tcp://127.0.0.1:48103");
        TrackingSpotEndpoint = value(properties, "trackingSpotEndpoint", "tcp://127.0.0.1:48118");
        TrackingSpotPubEndpoint = value(properties, "trackingSpotPubEndpoint", "tcp://127.0.0.1:48120");
        CustomerStreamEndpoint = value(properties, "customerStreamEndpoint", "tcp://127.0.0.1:48104");
        CourierStreamEndpoint = value(properties, "courierStreamEndpoint", "tcp://127.0.0.1:48105");
        DispatchHttpEndpoint = value(properties, "dispatchHttpEndpoint", "http://127.0.0.1:48107");
        DispatchChannelEndpoint = value(properties, "dispatchChannelEndpoint", "tcp://127.0.0.1:48121");
        CustomerSpotEndpoint = value(properties, "customerSpotEndpoint", "tcp://127.0.0.1:48109");
        CustomerSpotRouterEndpoint = value(properties, "customerSpotRouterEndpoint", "tcp://127.0.0.1:48110");
        CustomerSpotNodeRid = value(properties, "customerSpotNodeRid", "customer-node-1");
        CourierNodeRid = value(properties, "courierNodeRid", "courier-node-1");
        CourierActorNode1Rid = value(properties, "courierActorNode1Rid", "courier-node-1");
        CourierActorNode2Rid = value(properties, "courierActorNode2Rid", "courier-node-2");
        CourierActorNode1SpotEndpoint = value(properties, "courierActorNode1SpotEndpoint", "tcp://127.0.0.1:48113");
        CourierActorNode2SpotEndpoint = value(properties, "courierActorNode2SpotEndpoint", "tcp://127.0.0.1:48114");
        CourierActorNode1RouterEndpoint = value(properties, "courierActorNode1RouterEndpoint", "tcp://127.0.0.1:48115");
        CourierActorNode2RouterEndpoint = value(properties, "courierActorNode2RouterEndpoint", "tcp://127.0.0.1:48116");
        CourierSessionSpotRouterEndpoint = value(properties, "courierSessionSpotRouterEndpoint", "tcp://127.0.0.1:48117");
        CourierSessionSpotEndpoint = value(properties, "courierSessionSpotEndpoint", "tcp://127.0.0.1:48119");
        CourierSessionSpotNodeRid = value(properties, "courierSessionSpotNodeRid", "courier-session-node");
        RedisEndpoint = required(properties, "redisEndpoint");
        RedisKeyPrefix = value(properties, "redisKeyPrefix", "deliverydispatch:java:");
        CourierNode = value(properties, "courierNode", "node1");
        LogDirectory = required(properties, "logDirectory");
    }

    public static String courierPlacement(String courierId) {
        return "courier-b".equals(courierId) ? CourierActorNode2Rid : CourierActorNode1Rid;
    }

    private static Properties load(String[] args) {
        if (args.length != 2 || !"--config".equals(args[0]) || args[1].isBlank()) {
            throw new IllegalArgumentException("Usage: <role> --config <path>");
        }
        Properties properties = new Properties();
        try (Reader reader = Files.newBufferedReader(Path.of(args[1]))) {
            properties.load(reader);
            return properties;
        } catch (Exception error) {
            throw new IllegalStateException("Could not load DeliveryDispatch sample config.", error);
        }
    }

    private static String value(Properties properties, String name, String fallback) {
        String value = properties.getProperty(name);
        return value == null || value.isBlank() ? fallback : value;
    }

    private static String required(Properties properties, String name) {
        String value = properties.getProperty(name);
        if (value == null || value.isBlank()) {
            throw new IllegalArgumentException("Missing DeliveryDispatch sample config: " + name);
        }
        return value;
    }
}
