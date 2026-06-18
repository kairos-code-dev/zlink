package systems.zlink.samples.shoppingmall.server.configuration;

public final class SampleNames {
    public static final String CommerceApiChannel = "shoppingmall.commerce.api";
    public static final String OrderWorkflowChannel = "shoppingmall.order.workflow";

    public static final String ApiInstanceA = "api-a";
    public static final String ApiInstanceB = "api-b";
    public static final String WorkflowInstanceA = "workflow-a";
    public static final String WorkflowInstanceB = "workflow-b";

    private SampleNames() {
    }

    /** Per-instance workflow channel name (CommerceApi client / OrderWorkflow server). */
    public static String workflowChannel(String instanceId) {
        return OrderWorkflowChannel + "." + instanceId;
    }

    /** Per-instance CommerceApi channel name (public surface and peer forwarding). */
    public static String commerceApiChannel(String instanceId) {
        return CommerceApiChannel + "." + instanceId;
    }
}
