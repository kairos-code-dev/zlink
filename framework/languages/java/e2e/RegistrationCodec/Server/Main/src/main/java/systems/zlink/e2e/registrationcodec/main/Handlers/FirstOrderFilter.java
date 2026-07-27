package systems.zlink.e2e.registrationcodec.main.Handlers;

import java.util.concurrent.CompletionStage;
import systems.zlink.e2e.registrationcodec.main.Infrastructure.EvidenceStore;
import systems.zlink.framework.ZLinkHandlerFilter;
import systems.zlink.framework.ZLinkHandlerFilterNext;
import systems.zlink.framework.ZLinkMessageContext;

public final class FirstOrderFilter implements ZLinkHandlerFilter {
    private final EvidenceStore state;

    public FirstOrderFilter(EvidenceStore state) {
        this.state = state;
    }

    @Override
    public <T> CompletionStage<T> invoke(
        ZLinkMessageContext context,
        ZLinkHandlerFilterNext<T> next) {
        record(context, "first-before");
        return next.invoke()
            .whenComplete((ignored, error) -> record(context, "first-after"));
    }

    private void record(ZLinkMessageContext context, String step) {
        state.record("Filter", context.packetName(), step);
    }
}
