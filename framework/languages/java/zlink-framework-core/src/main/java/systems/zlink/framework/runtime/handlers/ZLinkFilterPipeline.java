package systems.zlink.framework.runtime.handlers;

import systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerActivator;

import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletionStage;
import java.util.function.Supplier;
import systems.zlink.framework.ZLinkHandlerContext;
import systems.zlink.framework.ZLinkHandlerFilter;
import systems.zlink.framework.ZLinkInvocationContext;
import systems.zlink.framework.ZLinkNext;

public final class ZLinkFilterPipeline {
    private ZLinkFilterPipeline() {
    }

    public static <T> CompletionStage<T> invoke(
        List<Class<? extends ZLinkHandlerFilter>> filterTypes,
        ZLinkHandlerActivator handlerFactory,
        ZLinkHandlerContext context,
        Object request,
        Supplier<CompletionStage<T>> terminal) {
        ZLinkNext<T> next = terminal::get;
        ZLinkInvocationContext invocation =
            new DefaultInvocationContext(context, request);
        for (int index = filterTypes.size() - 1; index >= 0; index--) {
            Class<? extends ZLinkHandlerFilter> filterType = filterTypes.get(index);
            ZLinkNext<T> currentNext = next;
            next = () -> {
                @SuppressWarnings("unchecked")
                ZLinkHandlerFilter filter =
                    (ZLinkHandlerFilter) handlerFactory.create(filterType);
                return filter.invoke(invocation, currentNext);
            };
        }
        return next.invoke();
    }

    private static final class DefaultInvocationContext implements ZLinkInvocationContext {
        private final ZLinkHandlerContext inner;
        private final Object request;

        private DefaultInvocationContext(ZLinkHandlerContext inner, Object request) {
            this.inner = inner;
            this.request = request;
        }

        @Override
        public Optional<Object> request() {
            return Optional.ofNullable(request);
        }

        @Override
        public Optional<String> channelName() {
            return inner.channelName();
        }

        @Override
        public Optional<String> packetName() {
            return inner.packetName();
        }

        @Override
        public Optional<String> contentType() {
            return inner.contentType();
        }

        @Override
        public Map<String, String> metadata() {
            return inner.metadata();
        }
    }
}
