package systems.zlink.framework.kotlin

import java.util.concurrent.CompletionStage
import kotlinx.coroutines.future.await
import systems.zlink.framework.execution.ZLinkFrameworkTurns

public suspend fun <T> CompletionStage<T>.await(): T =
    awaitFrameworkStage(this)

@PublishedApi
internal suspend fun <T> awaitFrameworkStage(stage: CompletionStage<T>): T {
    val turn = ZLinkFrameworkTurns.captureCurrent()
    return if (turn == null) {
        stage.await()
    } else {
        ZLinkFrameworkTurns.awaitManagedCompletion(turn, stage)
            .toCompletableFuture()
            .join()
    }
}
