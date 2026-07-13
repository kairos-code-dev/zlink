package systems.zlink.framework.runtime.internal.handlers;

/** Internal handler state that coroutine adapters must restore on every resume. */
public final class ZLinkSuspendInvocationContext {
    private static final ThreadLocal<Object> ENTRY_SPOT_DISPATCH = new ThreadLocal<>();
    private static final ThreadLocal<Object> SPOT_OUTBOUND = new ThreadLocal<>();
    private static final ThreadLocal<String> ACTOR_DISPATCH = new ThreadLocal<>();

    private ZLinkSuspendInvocationContext() {
    }

    public static Object currentEntrySpotDispatch() {
        return ENTRY_SPOT_DISPATCH.get();
    }

    public static ThreadLocal<Object> entrySpotDispatchThreadLocal() {
        return ENTRY_SPOT_DISPATCH;
    }

    public static Object currentSpotOutbound() {
        return SPOT_OUTBOUND.get();
    }

    public static ThreadLocal<Object> spotOutboundThreadLocal() {
        return SPOT_OUTBOUND;
    }

    public static String currentActorDispatch() {
        return ACTOR_DISPATCH.get();
    }

    public static ThreadLocal<String> actorDispatchThreadLocal() {
        return ACTOR_DISPATCH;
    }

    public static Scope enterEntrySpotDispatch(Object context) {
        Object previous = ENTRY_SPOT_DISPATCH.get();
        if (context == null) {
            ENTRY_SPOT_DISPATCH.remove();
        } else {
            ENTRY_SPOT_DISPATCH.set(context);
        }
        return () -> {
            if (previous == null) {
                ENTRY_SPOT_DISPATCH.remove();
            } else {
                ENTRY_SPOT_DISPATCH.set(previous);
            }
        };
    }

    public static Scope enterSpotOutbound(Object outbound) {
        return enter(SPOT_OUTBOUND, outbound);
    }

    public static Scope enterActorDispatch(String actorId) {
        return enter(ACTOR_DISPATCH, actorId);
    }

    private static <T> Scope enter(ThreadLocal<T> local, T value) {
        T previous = local.get();
        if (value == null) {
            local.remove();
        } else {
            local.set(value);
        }
        return () -> {
            if (previous == null) {
                local.remove();
            } else {
                local.set(previous);
            }
        };
    }

    @FunctionalInterface
    public interface Scope extends AutoCloseable {
        @Override
        void close();
    }
}
