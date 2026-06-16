package systems.zlink.samples.shoppingmallcheckout.server.configuration;

import systems.zlink.samples.shoppingmallcheckout.shared.contracts.Messages;

/**
 * Endpoint topology and {@code OrderId} owner routing.
 *
 * <p>The run script reserves free TCP ports and overrides the defaults below
 * through {@code -Dzlink.samples.shoppingmallcheckout.*} system properties.
 */
public final class SampleTopology {
    public static final String RegistryPubEndpoint =
        property("registryPubEndpoint", "tcp://127.0.0.1:47490");
    public static final String RegistryRouterEndpoint =
        property("registryRouterEndpoint", "tcp://127.0.0.1:47491");

    public static final String CommerceApiAEndpoint =
        property("commerceApiAEndpoint", "tcp://127.0.0.1:47492");
    public static final String CommerceApiBEndpoint =
        property("commerceApiBEndpoint", "tcp://127.0.0.1:47493");

    public static final String WorkflowAEndpoint =
        property("workflowAEndpoint", "tcp://127.0.0.1:47494");
    public static final String WorkflowBEndpoint =
        property("workflowBEndpoint", "tcp://127.0.0.1:47495");

    private SampleTopology() {
    }

    public static String commerceApiEndpoint(String instanceId) {
        return switch (instanceId) {
            case SampleNames.ApiInstanceB -> CommerceApiBEndpoint;
            default -> CommerceApiAEndpoint;
        };
    }

    public static String workflowEndpoint(String instanceId) {
        return switch (instanceId) {
            case SampleNames.WorkflowInstanceB -> WorkflowBEndpoint;
            default -> WorkflowAEndpoint;
        };
    }

    /**
     * Deterministic, cross-language stable owner selection by {@code OrderId}.
     *
     * <p>The .NET sample uses the runtime ordinal string hash; here a fixed
     * character-sum keeps the same {@code OrderId} pinned to one workflow owner
     * while distributing the sample order ids across both instances.
     */
    public static String workflowInstanceForOrder(String orderId) {
        int sum = 0;
        for (int i = 0; i < orderId.length(); i++) {
            sum = (sum * 31 + orderId.charAt(i)) & 0x7fffffff;
        }
        return (sum % 2) == 1 ? SampleNames.WorkflowInstanceB : SampleNames.WorkflowInstanceA;
    }

    public static String workflowChannelEndpointForOrder(String orderId) {
        return workflowEndpoint(workflowInstanceForOrder(orderId));
    }

    public static Messages.OrderState failedPlaceholder(String orderId) {
        return new Messages.OrderState(
            orderId,
            Messages.OrderStatuses.Failed,
            null,
            null,
            null,
            "order does not exist",
            null,
            null,
            System.currentTimeMillis());
    }

    private static String property(String name, String defaultValue) {
        return System.getProperty("zlink.samples.shoppingmallcheckout." + name, defaultValue);
    }
}
