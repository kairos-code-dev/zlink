package systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.actors

import java.util.Optional
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.actors.ZLinkActorContext
import systems.zlink.framework.actors.ZLinkBoundSession
import systems.zlink.framework.spots.ZLinkSpot

class RecordingActorContext : ZLinkActorContext {
    private val pushes = mutableListOf<String>()

    override fun spotRid(): Optional<RoutingId> = Optional.of(RoutingId.from("room-spot"))
    override fun isJoined(): Boolean = true
    override fun boundSession(): ZLinkBoundSession = RecordingBoundSession(pushes)
    override fun getSpot(): ZLinkSpot = throw UnsupportedOperationException("not needed by sample")
    override fun <TSpot : ZLinkSpot> getSpot(spotType: Class<TSpot>): TSpot =
        throw UnsupportedOperationException("not needed by sample")

    fun pushes(): List<String> = pushes.toList()
}
