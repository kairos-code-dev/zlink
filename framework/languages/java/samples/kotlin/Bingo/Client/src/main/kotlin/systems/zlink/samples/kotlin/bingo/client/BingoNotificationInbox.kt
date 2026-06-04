package systems.zlink.samples.kotlin.bingo.client

class BingoNotificationInbox {
    private val events = mutableListOf<String>()

    fun add(event: String) {
        events += event
    }

    fun events(): List<String> = events.toList()
}
