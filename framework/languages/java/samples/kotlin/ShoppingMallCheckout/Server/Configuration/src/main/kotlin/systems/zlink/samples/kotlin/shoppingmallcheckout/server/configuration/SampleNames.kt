package systems.zlink.samples.kotlin.shoppingmallcheckout.server.configuration

object SampleNames {
    const val CommerceApiChannel = "shoppingmall.commerce.api"
    const val OrderWorkflowChannel = "shoppingmall.order.workflow"

    const val ApiInstanceA = "api-a"
    const val ApiInstanceB = "api-b"
    const val WorkflowInstanceA = "workflow-a"
    const val WorkflowInstanceB = "workflow-b"

    fun workflowChannel(instanceId: String): String = "$OrderWorkflowChannel.$instanceId"

    fun commerceApiChannel(instanceId: String): String = "$CommerceApiChannel.$instanceId"
}
