package systems.zlink.samples.kotlin.deliverydispatch.server.dispatchapi.handlers

import org.springframework.web.bind.annotation.PostMapping
import org.springframework.web.bind.annotation.RequestBody
import org.springframework.web.bind.annotation.RestController
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.EvidenceStore
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatuses
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.ServerAssertionReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.ServerAssertionRes

@RestController
class ServerAssertionHandler(
    private val evidence: EvidenceStore,
) {
    @PostMapping("/self-check/assert")
    fun handle(@RequestBody request: ServerAssertionReq): ServerAssertionRes {
        val success = evidence.hasSequence(
            request.successfulDeliveryId,
            DeliveryStatuses.Assigned,
            DeliveryStatuses.Accepted,
            DeliveryStatuses.PickedUp,
            DeliveryStatuses.Delivered,
        )
        val reassigned = evidence.hasSequence(
            request.reassignedDeliveryId,
            DeliveryStatuses.Assigned,
            DeliveryStatuses.Reassigned,
            DeliveryStatuses.Accepted,
            DeliveryStatuses.PickedUp,
            DeliveryStatuses.Delivered,
        )
        return ServerAssertionRes(success && reassigned, evidence.readLines())
    }
}
