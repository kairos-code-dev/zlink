package systems.zlink.samples.kotlin.tictactoe.sessiongateway.client

class SessionActorNotificationInbox {
    private val events = mutableListOf<String>()

    fun addAll(values: List<String>) {
        events += values
    }

    fun events(): List<String> = events.toList()
}
