package systems.zlink.e2e.kotlin.registrationcodec.main.handlers

import java.util.Optional
import java.util.concurrent.CompletionStage
import systems.zlink.e2e.kotlin.registrationcodec.EchoManualReq
import systems.zlink.e2e.kotlin.registrationcodec.main.infrastructure.ScenarioState
import systems.zlink.framework.ZLinkHandlerFilter
import systems.zlink.framework.ZLinkInvocationContext
import systems.zlink.framework.ZLinkNext

private object FilterOrderValues {
    fun from(request: Any): Optional<String> =
        if (request is EchoManualReq && request.value.startsWith("filter-order")) {
            Optional.of(request.value)
        } else {
            Optional.empty()
        }
}

class FirstOrderFilter(
    private val state: ScenarioState,
) : ZLinkHandlerFilter {
    override fun <T : Any?> invoke(
        context: ZLinkInvocationContext,
        next: ZLinkNext<T>,
    ): CompletionStage<T> {
        record(context, "first-before")
        return next.invoke().whenComplete { _, _ -> record(context, "first-after") }
    }

    private fun record(context: ZLinkInvocationContext, step: String) {
        context.request()
            .flatMap(FilterOrderValues::from)
            .ifPresent { value -> state.record("Filter", "EchoManualReq", "$step:$value") }
    }
}

class SecondOrderFilter(
    private val state: ScenarioState,
) : ZLinkHandlerFilter {
    override fun <T : Any?> invoke(
        context: ZLinkInvocationContext,
        next: ZLinkNext<T>,
    ): CompletionStage<T> {
        record(context, "second-before")
        return next.invoke().whenComplete { _, _ -> record(context, "second-after") }
    }

    private fun record(context: ZLinkInvocationContext, step: String) {
        context.request()
            .flatMap(FilterOrderValues::from)
            .ifPresent { value -> state.record("Filter", "EchoManualReq", "$step:$value") }
    }
}
