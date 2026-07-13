package systems.zlink.e2e.registrationcodec.main.Handlers;

import java.util.concurrent.CompletionStage;
import systems.zlink.e2e.registrationcodec.main.Infrastructure.EvidenceStore;
import systems.zlink.framework.ZLinkHandlerFilter;
import systems.zlink.framework.ZLinkInvocationContext;
import systems.zlink.framework.ZLinkNext;

public final class FirstOrderFilter implements ZLinkHandlerFilter {
    private final EvidenceStore state;

    public FirstOrderFilter(EvidenceStore state) {
        this.state = state;
    }

    @Override
    public <T> CompletionStage<T> invoke(
        ZLinkInvocationContext context,
        ZLinkNext<T> next) {
        record(context, "first-before");
        return next.invoke()
            .whenComplete((ignored, error) -> record(context, "first-after"));
    }

    private void record(ZLinkInvocationContext context, String step) {
        context.request()
            .flatMap(FilterOrderValues::from)
            .ifPresent(value -> state.record("Filter", "EchoManual", step + ":" + value));
    }
}
