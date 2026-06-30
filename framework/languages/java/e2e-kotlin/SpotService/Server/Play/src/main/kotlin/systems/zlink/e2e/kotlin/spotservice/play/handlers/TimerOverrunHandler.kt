package systems.zlink.e2e.kotlin.spotservice.play.handlers

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.ScenarioState
import systems.zlink.e2e.kotlin.spotservice.play.spots.*
import systems.zlink.framework.spots.ZLinkSpotTimerHandler
import systems.zlink.framework.spots.ZLinkTimerTick

class TimerOverrunHandler : ZLinkSpotTimerHandler<TimerScenarioSpot> {
    override fun handle(spot: TimerScenarioSpot, tick: ZLinkTimerTick) {
        try {
            Thread.sleep(160)
        } catch (error: InterruptedException) {
            Thread.currentThread().interrupt()
            throw IllegalStateException("timer overrun interrupted", error)
        }
        spot.overrunTick(tick.deliveryIndex(), tick.skippedTicks())
    }
}
