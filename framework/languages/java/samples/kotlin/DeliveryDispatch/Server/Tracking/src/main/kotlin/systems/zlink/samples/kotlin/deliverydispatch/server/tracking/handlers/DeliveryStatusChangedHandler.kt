package systems.zlink.samples.kotlin.deliverydispatch.server.tracking.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.DeliveryEvidenceStore
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatusChangedRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatusChangedReq

@ZLinkHandlerGroup("tracking")
class DeliveryStatusChangedHandler(
    private val evidenceStore: DeliveryEvidenceStore,
    private val channels: ZLinkClient,
) : ZLinkRequestHandler<DeliveryStatusChangedReq, DeliveryStatusChangedRes> {
    override fun handle(
        request: DeliveryStatusChangedReq,
        context: ZLinkRequestContext,
    ): DeliveryStatusChangedRes {
        evidenceStore.append(request)
        channels
            .requestToChannel(SampleNames.CustomerRouteChannel, request)
            .await(DeliveryStatusChangedRes::class.java)
        return DeliveryStatusChangedRes(request.deliveryId, request.status)
    }
}
