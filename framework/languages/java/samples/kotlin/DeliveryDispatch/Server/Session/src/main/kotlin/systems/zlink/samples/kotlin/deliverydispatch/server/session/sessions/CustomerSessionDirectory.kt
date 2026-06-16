package systems.zlink.samples.kotlin.deliverydispatch.server.session.sessions

import kotlinx.coroutines.future.await
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatusNotify

class CustomerSessionDirectory {
    private val gate = Any()
    private val sessions = HashMap<String, ZLinkSessionContext>()
    private val subscriptions = HashMap<String, MutableSet<String>>()

    fun add(context: ZLinkSessionContext) {
        synchronized(gate) {
            sessions[context.sessionId()] = context
        }
    }

    fun remove(context: ZLinkSessionContext) {
        synchronized(gate) {
            sessions.remove(context.sessionId())
            subscriptions.values.forEach { it.remove(context.sessionId()) }
        }
    }

    fun subscribe(context: ZLinkSessionContext, deliveryId: String) {
        synchronized(gate) {
            sessions[context.sessionId()] = context
            subscriptions.getOrPut(deliveryId) { HashSet() }.add(context.sessionId())
        }
    }

    suspend fun publish(status: DeliveryStatusNotify) {
        val targets =
            synchronized(gate) {
                val subscribers = subscriptions[status.deliveryId] ?: return
                subscribers.mapNotNull { sessions[it] }
            }
        for (target in targets) {
            target.client().send(status).submit().await()
        }
    }
}
