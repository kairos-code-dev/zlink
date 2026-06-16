package systems.zlink.samples.kotlin.deliverydispatch.server.dispatchcenter

import java.util.concurrent.LinkedBlockingQueue
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.AssignDelivery

class DispatchWorkQueue {
    private val poison = AssignDelivery("", "", "", "")
    private val queue = LinkedBlockingQueue<AssignDelivery>()

    fun enqueue(delivery: AssignDelivery) {
        queue.add(delivery)
    }

    fun take(): AssignDelivery? {
        val delivery = queue.take()
        return if (delivery === poison) null else delivery
    }

    fun signalStop() {
        queue.add(poison)
    }
}
