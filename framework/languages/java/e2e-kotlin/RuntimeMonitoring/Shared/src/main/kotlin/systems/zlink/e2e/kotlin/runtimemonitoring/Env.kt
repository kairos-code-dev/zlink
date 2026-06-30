package systems.zlink.e2e.kotlin.runtimemonitoring

object Env {
    @JvmStatic
    @JvmOverloads
    fun get(
        name: String,
        fallback: String = "",
    ): String {
        val value = System.getenv(name)
        if (value.isNullOrBlank()) {
            return fallback
        }
        return value
    }
}
