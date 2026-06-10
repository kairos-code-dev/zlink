package systems.zlink.framework.kotlin

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import systems.zlink.framework.configuration.ZLinkFrameworkOptions

fun ZLinkFrameworkOptions.useCoroutineHandlers(dispatcher: CoroutineDispatcher) {
    useSuspendHandlerInvoker(
        ZLinkCoroutineSuspendHandlerInvoker(ZLinkCoroutineRuntime(dispatcher)),
    )
}

fun ZLinkFrameworkOptions.useCoroutineHandlers(
    scope: CoroutineScope,
    dispatcher: CoroutineDispatcher,
) {
    useSuspendHandlerInvoker(
        ZLinkCoroutineSuspendHandlerInvoker(ZLinkCoroutineRuntime(scope, dispatcher)),
    )
}
