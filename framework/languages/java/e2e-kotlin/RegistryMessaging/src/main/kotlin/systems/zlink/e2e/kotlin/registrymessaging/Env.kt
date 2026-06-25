package systems.zlink.e2e.kotlin.registrymessaging

object Env {
    fun get(name: String): String = get(name, "")

    fun get(name: String, fallback: String): String {
        val value = System.getenv(name)
        return if (value.isNullOrBlank()) fallback else value
    }
}
