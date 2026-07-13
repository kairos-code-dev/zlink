package systems.zlink.framework.kotlin

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.asContextElement
import kotlin.coroutines.CoroutineContext
import systems.zlink.framework.runtime.internal.diagnostics.ZLinkFlowContext
import systems.zlink.framework.runtime.internal.handlers.ZLinkSuspendInvocationContext

internal object ZLinkCoroutineInvocationContext {
    fun capture(dispatcher: CoroutineDispatcher): CoroutineContext =
        dispatcher +
            ZLinkSuspendInvocationContext.entrySpotDispatchThreadLocal()
                .asContextElement(ZLinkSuspendInvocationContext.currentEntrySpotDispatch()) +
            ZLinkSuspendInvocationContext.spotOutboundThreadLocal()
                .asContextElement(ZLinkSuspendInvocationContext.currentSpotOutbound()) +
            ZLinkSuspendInvocationContext.actorDispatchThreadLocal()
                .asContextElement(ZLinkSuspendInvocationContext.currentActorDispatch()) +
            ZLinkFlowContext.threadLocal().asContextElement(ZLinkFlowContext.current())
}
