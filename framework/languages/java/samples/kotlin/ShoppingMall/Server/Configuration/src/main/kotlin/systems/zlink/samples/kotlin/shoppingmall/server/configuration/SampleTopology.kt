package systems.zlink.samples.kotlin.shoppingmall.server.configuration

import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.OrderState
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.OrderStatuses

/**
 * Endpoint topology and `OrderId` owner routing. The run script overrides the
 * defaults through `-Dzlink.samples.shoppingmall.*` system properties.
 */
object SampleTopology {
    val RegistryPubEndpoint = property("registryPubEndpoint", "tcp://127.0.0.1:47490")
    val RegistryRouterEndpoint = property("registryRouterEndpoint", "tcp://127.0.0.1:47491")
    val CommerceApiAEndpoint = property("commerceApiAEndpoint", "tcp://127.0.0.1:47492")
    val CommerceApiBEndpoint = property("commerceApiBEndpoint", "tcp://127.0.0.1:47493")
    val WorkflowAEndpoint = property("workflowAEndpoint", "tcp://127.0.0.1:47494")
    val WorkflowBEndpoint = property("workflowBEndpoint", "tcp://127.0.0.1:47495")

    fun commerceApiEndpoint(instanceId: String): String =
        if (instanceId == SampleNames.ApiInstanceB) CommerceApiBEndpoint else CommerceApiAEndpoint

    fun workflowEndpoint(instanceId: String): String =
        if (instanceId == SampleNames.WorkflowInstanceB) WorkflowBEndpoint else WorkflowAEndpoint

    /** Deterministic, cross-language stable owner selection by `OrderId`. */
    fun workflowInstanceForOrder(orderId: String): String {
        var sum = 0
        for (c in orderId) {
            sum = (sum * 31 + c.code) and 0x7fffffff
        }
        return if (sum % 2 == 1) SampleNames.WorkflowInstanceB else SampleNames.WorkflowInstanceA
    }

    fun failedPlaceholder(orderId: String): OrderState =
        OrderState(
            orderId,
            OrderStatuses.Failed,
            null,
            null,
            null,
            "order does not exist",
            null,
            null,
            System.currentTimeMillis(),
        )

    private fun property(name: String, defaultValue: String): String =
        System.getProperty("zlink.samples.shoppingmall.$name", defaultValue)
}
