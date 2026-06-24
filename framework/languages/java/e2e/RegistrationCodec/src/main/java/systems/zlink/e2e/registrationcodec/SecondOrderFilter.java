package systems.zlink.e2e.registrationcodec;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.ZLinkHandlerFilter;
import systems.zlink.framework.ZLinkInvocationContext;
import systems.zlink.framework.ZLinkNext;

public final class SecondOrderFilter implements ZLinkHandlerFilter {
    private final ScenarioState state;

    public SecondOrderFilter(ScenarioState state) {
        this.state = state;
    }

    @Override
    public <T> CompletionStage<T> invokeAsync(
        ZLinkInvocationContext context,
        ZLinkNext<T> next) {
        record(context, "second-before");
        return next.invokeAsync()
            .whenComplete((ignored, error) -> record(context, "second-after"));
    }

    private void record(ZLinkInvocationContext context, String step) {
        context.request()
            .flatMap(FilterOrderValues::from)
            .ifPresent(value -> state.record("Filter", "EchoManual", step + ":" + value));
    }
}
