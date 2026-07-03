package systems.zlink.framework.testkit;

import java.lang.reflect.Constructor;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterFactory;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;

final class RuntimeTestSupport {
    private RuntimeTestSupport() {
    }

    static ZLinkFrameworkRuntime startFramework(
        DefaultZLinkFrameworkOptions options,
        ZLinkBackendAdapterFactory backendFactory) {
        return invoke(() -> {
            Method start = ZLinkFrameworkRuntime.class.getDeclaredMethod(
                "start",
                DefaultZLinkFrameworkOptions.class,
                ZLinkBackendAdapterFactory.class);
            start.setAccessible(true);
            return (ZLinkFrameworkRuntime) start.invoke(null, options, backendFactory);
        });
    }

    static ZLinkFrameworkRuntime newFrameworkRuntime(
        DefaultZLinkFrameworkOptions options,
        ZLinkBackendAdapterFactory backendFactory,
        ZLinkMessageSerializer serializer,
        ZLinkHandlerFactory handlerFactory) {
        return invoke(() -> {
            Constructor<ZLinkFrameworkRuntime> constructor =
                ZLinkFrameworkRuntime.class.getDeclaredConstructor(
                    DefaultZLinkFrameworkOptions.class,
                    ZLinkBackendAdapterFactory.class,
                    ZLinkMessageSerializer.class,
                    ZLinkHandlerFactory.class);
            constructor.setAccessible(true);
            return constructor.newInstance(options, backendFactory, serializer, handlerFactory);
        });
    }

    private static <T> T invoke(ReflectiveCall<T> call) {
        try {
            return call.invoke();
        } catch (InvocationTargetException error) {
            Throwable cause = error.getCause();
            if (cause instanceof RuntimeException runtimeException) {
                throw runtimeException;
            }
            if (cause instanceof Error fatal) {
                throw fatal;
            }
            throw new IllegalStateException(cause);
        } catch (ReflectiveOperationException error) {
            throw new IllegalStateException(error);
        }
    }

    private interface ReflectiveCall<T> {
        T invoke() throws ReflectiveOperationException;
    }
}
