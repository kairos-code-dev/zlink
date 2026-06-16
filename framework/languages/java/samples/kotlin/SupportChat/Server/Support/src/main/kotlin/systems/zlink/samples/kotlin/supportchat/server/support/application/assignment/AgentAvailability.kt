package systems.zlink.samples.kotlin.supportchat.server.support.application.assignment

import java.util.ArrayDeque

data class AvailableAgent(val actorId: String, val displayName: String)

class AgentAvailabilityDirectory {
    private val gate = Any()
    private val available = ArrayDeque<AvailableAgent>()
    private val actorIds = mutableSetOf<String>()

    fun setAvailable(actorId: String, displayName: String, isAvailable: Boolean) {
        synchronized(gate) {
            if (!isAvailable) {
                actorIds.remove(actorId)
                return
            }
            if (actorIds.add(actorId)) {
                available.add(AvailableAgent(actorId, displayName))
            }
        }
    }

    fun takeNext(): AvailableAgent? =
        synchronized(gate) {
            var candidate = available.poll()
            while (candidate != null) {
                if (actorIds.remove(candidate.actorId)) {
                    return candidate
                }
                candidate = available.poll()
            }
            null
        }
}

class AgentAssignmentService(
    private val availability: AgentAvailabilityDirectory,
) {
    fun assignNextAgent(): AvailableAgent? = availability.takeNext()
}
