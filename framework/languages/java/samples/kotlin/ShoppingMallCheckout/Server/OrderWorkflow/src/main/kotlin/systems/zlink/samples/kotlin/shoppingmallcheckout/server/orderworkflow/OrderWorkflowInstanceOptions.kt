package systems.zlink.samples.kotlin.shoppingmallcheckout.server.orderworkflow

import systems.zlink.samples.kotlin.shoppingmallcheckout.server.configuration.SampleNames

/** Parsed `--instance` selector for an OrderWorkflow process. */
data class OrderWorkflowInstanceOptions(val instanceId: String) {
    companion object {
        fun fromArgs(args: Array<String>): OrderWorkflowInstanceOptions {
            var instanceId = SampleNames.WorkflowInstanceA
            for (i in 0 until args.size - 1) {
                if (args[i] == "--instance") {
                    instanceId = args[i + 1]
                }
            }
            return OrderWorkflowInstanceOptions(instanceId)
        }
    }
}
