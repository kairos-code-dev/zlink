package systems.zlink.framework.runtime.handlers;

import systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerActivator;

import java.util.List;
import java.util.concurrent.CompletionStage;
import java.util.function.Supplier;
import systems.zlink.framework.ZLinkHandlerFilter;
import systems.zlink.framework.ZLinkHandlerFilterNext;
import systems.zlink.framework.ZLinkMessageContext;

public final class ZLinkFilterPipeline {
    private ZLinkFilterPipeline() {
    }

    public static <T> CompletionStage<T> invoke(
        List<Class<? extends ZLinkHandlerFilter>> filterTypes,
        ZLinkHandlerActivator handlerFactory,
        ZLinkMessageContext context,
        Supplier<CompletionStage<T>> terminal) {
        ZLinkHandlerFilterNext<T> next = terminal::get;
        for (int index = filterTypes.size() - 1; index >= 0; index--) {
            Class<? extends ZLinkHandlerFilter> filterType = filterTypes.get(index);
            ZLinkHandlerFilterNext<T> currentNext = next;
            next = () -> {
                @SuppressWarnings("unchecked")
                ZLinkHandlerFilter filter =
                    (ZLinkHandlerFilter) handlerFactory.create(filterType);
                return filter.invoke(context, currentNext);
            };
        }
        return next.invoke();
    }
}
