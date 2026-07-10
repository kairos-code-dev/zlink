package systems.zlink.e2e.kotlin.runtimemonitoring.service

import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.spots.ZLinkSpot
import systems.zlink.framework.spots.ZLinkSpotContext
import systems.zlink.framework.spots.ZLinkSpotCreateResponse
import systems.zlink.framework.spots.ZLinkSpotTimerHandler
import systems.zlink.framework.spots.ZLinkTimerOptions
import systems.zlink.framework.spots.ZLinkTimerTick
import java.time.Duration

class MonitoringSpot(
    private val context: ZLinkSpotContext,
) : ZLinkSpot<ZLinkActor> {
    override fun context(): ZLinkSpotContext = context

    override fun onCreate(request: ZLinkMessage): ZLinkSpotCreateResponse {
        val options = ZLinkTimerOptions()
        options.setStopOnUnhandledException(false)
        context.addTimer(
            "failing-monitoring-timer",
            Duration.ofMillis(500),
            FailingTimerHandler::class.java,
            options,
        )
        context.addTimer(
            "stopping-monitoring-timer",
            Duration.ofMillis(500),
            FailingTimerHandler::class.java,
            ZLinkTimerOptions(),
        )
        return ZLinkSpotCreateResponse.accept()
    }

    override fun onJoinedActor(actor: ZLinkActor, cancellationToken: CancellationToken) = Unit

    override fun onLeaveActor(actor: ZLinkActor, cancellationToken: CancellationToken) = Unit

    class FailingTimerHandler : ZLinkSpotTimerHandler<MonitoringSpot> {
        override fun handle(spot: MonitoringSpot, tick: ZLinkTimerTick) {
            throw IllegalStateException("monitoring timer boom")
        }
    }
}
