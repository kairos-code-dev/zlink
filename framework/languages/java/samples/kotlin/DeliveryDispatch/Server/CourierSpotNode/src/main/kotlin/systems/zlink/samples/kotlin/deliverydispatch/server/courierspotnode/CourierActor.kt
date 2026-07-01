package systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode

import java.util.concurrent.CompletableFuture
import java.util.concurrent.TimeUnit
import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorContext
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.CourierDecisionMsg
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.OfferDeliveryReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.OfferDeliveryNotify
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.OfferDeliveryRes

class CourierActor(
    private val id: String,
    private val actorContext: ZLinkActorContext,
) : ZLinkActor {
    private val pending = mutableMapOf<String, CompletableFuture<CourierDecisionMsg>>()

    override fun actorId(): String = id

    override fun context(): ZLinkActorContext = actorContext

    fun offer(offer: OfferDeliveryReq): OfferDeliveryRes {
        val decision = CompletableFuture<CourierDecisionMsg>()
        synchronized(pending) {
            pending[offer.deliveryId] = decision
        }
        return try {
            actorContext.boundSession()
                .send(
                    OfferDeliveryNotify(
                        courierId = offer.courierId,
                        deliveryId = offer.deliveryId,
                        pickupAddress = offer.pickupAddress,
                        dropoffAddress = offer.dropoffAddress,
                    ),
                )
                .submit()
            val accepted = decision.get(SampleTimings.CourierDecisionTimeout.toMillis(), TimeUnit.MILLISECONDS)
            OfferDeliveryRes(
                deliveryId = offer.deliveryId,
                courierId = offer.courierId,
                accepted = accepted.accepted,
                reason = accepted.reason,
            )
        } catch (ex: java.util.concurrent.TimeoutException) {
            OfferDeliveryRes(
                deliveryId = offer.deliveryId,
                courierId = offer.courierId,
                accepted = false,
                reason = "courier did not respond before dispatch timeout",
            )
        } catch (ex: InterruptedException) {
            Thread.currentThread().interrupt()
            throw IllegalStateException("courier offer was interrupted", ex)
        } catch (ex: java.util.concurrent.ExecutionException) {
            throw IllegalStateException("courier decision failed", ex)
        } finally {
            synchronized(pending) {
                pending.remove(offer.deliveryId)
            }
        }
    }

    fun complete(decision: CourierDecisionMsg) {
        val waiting = synchronized(pending) { pending[decision.deliveryId] }
        waiting?.complete(decision)
    }
}
