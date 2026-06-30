package systems.zlink.e2e.kotlin.registrymessaging.client.Support

object ScenarioAssert {
    fun that(condition: Boolean, message: String) {
        if (!condition) {
            throw IllegalStateException(message)
        }
    }

    fun countNewEvidence(after: Collection<String>, before: Collection<String>, prefix: String, marker: String): Int {
        val beforeCount = before.count { it.contains(prefix) && it.contains(marker) }
        val afterCount = after.count { it.contains(prefix) && it.contains(marker) }
        return afterCount - beforeCount
    }
}
