package systems.zlink.samples.kotlin.deliverydispatch.server.dispatchcenter

import java.util.concurrent.LinkedBlockingQueue
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.AssignDeliveryReq

class DispatchWorkQueue {
    private val poison = AssignDeliveryReq("", "", "", "")
    private val queue = LinkedBlockingQueue<AssignDeliveryReq>()

    fun enqueue(delivery: AssignDeliveryReq) {
        queue.add(delivery)
    }

    fun take(): AssignDeliveryReq? {
        val delivery = queue.take()
        return if (delivery === poison) null else delivery
    }

    fun signalStop() {
        queue.add(poison)
    }
}
