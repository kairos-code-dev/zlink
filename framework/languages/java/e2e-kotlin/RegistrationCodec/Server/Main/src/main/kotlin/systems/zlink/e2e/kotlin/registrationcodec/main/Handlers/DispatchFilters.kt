package systems.zlink.e2e.kotlin.registrationcodec.main.handlers

import java.util.concurrent.CompletionStage
import systems.zlink.e2e.kotlin.registrationcodec.main.infrastructure.ScenarioState
import systems.zlink.framework.ZLinkHandlerFilter
import systems.zlink.framework.ZLinkHandlerFilterNext
import systems.zlink.framework.ZLinkMessageContext

class FirstOrderFilter(
    private val state: ScenarioState,
) : ZLinkHandlerFilter {
    override fun <T : Any?> invoke(
        context: ZLinkMessageContext,
        next: ZLinkHandlerFilterNext<T>,
    ): CompletionStage<T> {
        record(context, "first-before")
        return next.invoke().whenComplete { _, _ -> record(context, "first-after") }
    }

    private fun record(context: ZLinkMessageContext, step: String) {
        state.record("Filter", context.packetName(), step)
    }
}

class SecondOrderFilter(
    private val state: ScenarioState,
) : ZLinkHandlerFilter {
    override fun <T : Any?> invoke(
        context: ZLinkMessageContext,
        next: ZLinkHandlerFilterNext<T>,
    ): CompletionStage<T> {
        record(context, "second-before")
        return next.invoke().whenComplete { _, _ -> record(context, "second-after") }
    }

    private fun record(context: ZLinkMessageContext, step: String) {
        state.record("Filter", context.packetName(), step)
    }
}
