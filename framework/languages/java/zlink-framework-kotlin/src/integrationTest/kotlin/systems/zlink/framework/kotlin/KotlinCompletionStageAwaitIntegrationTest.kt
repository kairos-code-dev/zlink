package systems.zlink.framework.kotlin

import java.util.concurrent.CompletableFuture
import kotlinx.coroutines.async
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.yield
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Test

class KotlinCompletionStageAwaitIntegrationTest {
    @Test
    fun `cancelling coroutine waiter does not cancel framework stage`() = runBlocking {
        val stage = CompletableFuture<String>()
        val waiter = async { stage.await() }
        yield()

        waiter.cancelAndJoin()

        assertFalse(stage.isCancelled)
        stage.complete("runtime-completed")
        assertEquals("runtime-completed", stage.join())
    }

    @Test
    fun `completion error is unwrapped at coroutine boundary`() = runBlocking {
        val expected = IllegalStateException("framework failure")
        val stage = CompletableFuture.failedFuture<String>(expected)

        val actual = runCatching { stage.await() }.exceptionOrNull()

        assertEquals(expected::class, actual!!::class)
        assertEquals(expected.message, actual.message)
    }

}
