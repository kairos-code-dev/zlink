package systems.zlink.samples.shoppingmall.server.configuration;

import java.nio.charset.StandardCharsets;
import systems.zlink.contracts.core.RoutingId;

public final class SampleTopology {
    public static final String ApiAHttpUrl = property("apiAHttpUrl", "http://127.0.0.1:49101");
    public static final String ApiBHttpUrl = property("apiBHttpUrl", "http://127.0.0.1:49102");
    public static final String WorkflowAHttpUrl = property("workflowAHttpUrl", "http://127.0.0.1:49111");
    public static final String WorkflowBHttpUrl = property("workflowBHttpUrl", "http://127.0.0.1:49112");
    public static final String WorkflowAChannelEndpoint =
        property("workflowAChannelEndpoint", "tcp://127.0.0.1:49121");
    public static final String WorkflowBChannelEndpoint =
        property("workflowBChannelEndpoint", "tcp://127.0.0.1:49122");
    public static final String WorkflowASpotEndpoint =
        property("workflowASpotEndpoint", "tcp://127.0.0.1:49131");
    public static final String WorkflowASpotRouterEndpoint =
        property("workflowASpotRouterEndpoint", "tcp://127.0.0.1:49132");
    public static final String WorkflowBSpotEndpoint =
        property("workflowBSpotEndpoint", "tcp://127.0.0.1:49133");
    public static final String WorkflowBSpotRouterEndpoint =
        property("workflowBSpotRouterEndpoint", "tcp://127.0.0.1:49134");
    public static final String RedisEndpoint = property("redisEndpoint", "127.0.0.1:6379");
    public static final String RedisKeyPrefix = property("redisKeyPrefix", "shoppingmall:java:");

    private SampleTopology() {
    }

    public static String apiName() {
        return System.getProperty("zlink.samples.shoppingmall.apiName", "api-a");
    }

    public static String workflowName() {
        return System.getProperty("zlink.samples.shoppingmall.workflowName", "workflow-a");
    }

    public static String selectedApiHttpUrl() {
        return "api-b".equals(apiName()) ? ApiBHttpUrl : ApiAHttpUrl;
    }

    public static String selectedWorkflowHttpUrl() {
        return "workflow-b".equals(workflowName()) ? WorkflowBHttpUrl : WorkflowAHttpUrl;
    }

    public static String selectedWorkflowChannelEndpoint() {
        return "workflow-b".equals(workflowName()) ? WorkflowBChannelEndpoint : WorkflowAChannelEndpoint;
    }

    public static String selectedWorkflowSpotEndpoint() {
        return "workflow-b".equals(workflowName()) ? WorkflowBSpotEndpoint : WorkflowASpotEndpoint;
    }

    public static String selectedWorkflowSpotRouterEndpoint() {
        return "workflow-b".equals(workflowName())
            ? WorkflowBSpotRouterEndpoint
            : WorkflowASpotRouterEndpoint;
    }

    public static RoutingId selectedWorkflowRoutingId() {
        return RoutingId.from(workflowName());
    }

    public static String ownerWorkflowName(String orderId) {
        return ownerIndex(orderId) == 1 ? "workflow-b" : "workflow-a";
    }

    public static String workflowChannelEndpoint(String workflowName) {
        return "workflow-b".equals(workflowName) ? WorkflowBChannelEndpoint : WorkflowAChannelEndpoint;
    }

    public static String workflowSpotRouterEndpoint(String workflowName) {
        return "workflow-b".equals(workflowName)
            ? WorkflowBSpotRouterEndpoint
            : WorkflowASpotRouterEndpoint;
    }

    public static RoutingId workflowRoutingId(String workflowName) {
        return RoutingId.from(workflowName);
    }

    private static int ownerIndex(String orderId) {
        int sum = 0;
        for (byte value : orderId.getBytes(StandardCharsets.UTF_8)) {
            sum += Byte.toUnsignedInt(value);
        }
        return sum % 2;
    }

    private static String property(String name, String fallback) {
        return System.getProperty("zlink.samples.shoppingmall." + name, fallback);
    }
}
