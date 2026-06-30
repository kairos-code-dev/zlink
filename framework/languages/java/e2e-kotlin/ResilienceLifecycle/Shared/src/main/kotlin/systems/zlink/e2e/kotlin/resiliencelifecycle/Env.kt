package systems.zlink.e2e.kotlin.resiliencelifecycle

object Env {
    @JvmStatic
    fun get(name: String): String =
        System.getenv(name) ?: throw IllegalStateException("missing env $name")

    @JvmStatic
    fun get(name: String, defaultValue: String): String =
        System.getenv(name) ?: defaultValue
}
