package systems.zlink.e2e.kotlin.spotservice

object Env {
    @JvmStatic
    fun get(name: String): String =
        get(name, "")

    @JvmStatic
    fun get(name: String, fallback: String): String {
        val value = System.getenv(name)
        return if (value.isNullOrBlank()) fallback else value
    }
}
