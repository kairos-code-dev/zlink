package systems.zlink.e2e.kotlin.spotservice.client.support

internal fun ensure(condition: Boolean, message: String) {
    if (!condition) {
        throw IllegalStateException(message)
    }
}

internal fun expectFailure(action: () -> Unit) {
    try {
        action()
    } catch (_: RuntimeException) {
        return
    }
    throw IllegalStateException("operation unexpectedly succeeded")
}
