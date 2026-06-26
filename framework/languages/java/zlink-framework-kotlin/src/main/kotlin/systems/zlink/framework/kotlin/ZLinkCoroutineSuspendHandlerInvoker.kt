package systems.zlink.framework.kotlin

import java.lang.reflect.InvocationTargetException
import java.lang.reflect.Method
import java.util.concurrent.CompletionStage
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.ThreadContextElement
import kotlinx.coroutines.future.future
import kotlin.coroutines.AbstractCoroutineContextElement
import kotlin.coroutines.Continuation
import kotlin.coroutines.CoroutineContext
import kotlin.coroutines.intrinsics.COROUTINE_SUSPENDED
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException
import kotlin.coroutines.suspendCoroutine
import systems.zlink.framework.execution.ZLinkYieldTurn
import systems.zlink.framework.execution.ZLinkFrameworkTurns
import systems.zlink.framework.runtime.handlers.ZLinkHandlerMethodInvoker
import systems.zlink.framework.runtime.handlers.ZLinkSuspendHandlerInvoker

class ZLinkCoroutineSuspendHandlerInvoker : ZLinkSuspendHandlerInvoker {
    private val scope: CoroutineScope
    private val dispatcher: CoroutineDispatcher

    @JvmOverloads
    constructor(dispatcher: CoroutineDispatcher = Dispatchers.Default) {
        this.dispatcher = dispatcher
        this.scope = CoroutineScope(SupervisorJob() + dispatcher)
    }

    @JvmOverloads
    constructor(
        scope: CoroutineScope,
        dispatcher: CoroutineDispatcher = Dispatchers.Default,
    ) {
        this.dispatcher = dispatcher
        this.scope = scope
    }

    override fun supports(method: Method): Boolean =
        ZLinkHandlerMethodInvoker.isKotlinSuspendMethod(method)

    @Suppress("UNCHECKED_CAST")
    override fun invoke(
        handler: Any,
        method: Method,
        logicalArguments: Array<Any>,
    ): CompletionStage<Any> {
        val turn = ZLinkFrameworkTurns.captureCurrent()
        return (
        scope.future(dispatcher + ZLinkYieldTurnContext(turn)) {
            invokeSuspend(handler, method, logicalArguments)
        } as CompletionStage<Any>
        )
    }

    private suspend fun invokeSuspend(
        handler: Any,
        method: Method,
        logicalArguments: Array<Any>,
    ): Any? =
        suspendCoroutine { continuation ->
            val arguments = arrayOfNulls<Any>(logicalArguments.size + 1)
            for (index in logicalArguments.indices) {
                arguments[index] = logicalArguments[index]
            }
            @Suppress("UNCHECKED_CAST")
            arguments[arguments.lastIndex] = continuation as Continuation<Any?>
            try {
                method.isAccessible = true
                val result = method.invoke(handler, *arguments)
                if (result !== COROUTINE_SUSPENDED) {
                    continuation.resume(if (result === Unit) null else result)
                }
            } catch (ex: InvocationTargetException) {
                continuation.resumeWithException(ex.targetException ?: ex)
            } catch (ex: Throwable) {
                continuation.resumeWithException(ex)
            }
        }

    private class ZLinkYieldTurnContext(
        private val turn: ZLinkYieldTurn?,
    ) : ThreadContextElement<AutoCloseable>,
        AbstractCoroutineContextElement(Key) {
        companion object Key : CoroutineContext.Key<ZLinkYieldTurnContext>

        override fun updateThreadContext(context: CoroutineContext): AutoCloseable =
            ZLinkFrameworkTurns.enterTurn(turn)

        override fun restoreThreadContext(
            context: CoroutineContext,
            oldState: AutoCloseable,
        ) {
            oldState.close()
        }
    }
}
