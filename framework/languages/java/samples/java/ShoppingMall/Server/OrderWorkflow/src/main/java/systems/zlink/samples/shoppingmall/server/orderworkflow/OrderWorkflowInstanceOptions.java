package systems.zlink.samples.shoppingmall.server.orderworkflow;

import systems.zlink.samples.shoppingmall.server.configuration.SampleNames;

/** Parsed {@code --instance} selector for an OrderWorkflow process. */
public record OrderWorkflowInstanceOptions(String instanceId) {
    public static OrderWorkflowInstanceOptions fromArgs(String[] args) {
        String instanceId = SampleNames.WorkflowInstanceA;
        for (int i = 0; i < args.length - 1; i++) {
            if ("--instance".equals(args[i])) {
                instanceId = args[i + 1];
            }
        }
        return new OrderWorkflowInstanceOptions(instanceId);
    }
}
