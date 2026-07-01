package systems.zlink.samples.kotlin.deliverydispatch.server.customergateway.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.deliverydispatch.server.customergateway.CustomerActorDirectory
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatusChangedRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatusChangedReq

@ZLinkHandlerGroup("customer-route")
class CustomerStatusPushHandler(
    private val customers: CustomerActorDirectory,
) : ZLinkRequestHandler<DeliveryStatusChangedReq, DeliveryStatusChangedRes> {
    override fun handle(
        request: DeliveryStatusChangedReq,
        context: ZLinkRequestContext,
    ): DeliveryStatusChangedRes {
        customers.push(request)
        return DeliveryStatusChangedRes(request.deliveryId, request.status)
    }
}
