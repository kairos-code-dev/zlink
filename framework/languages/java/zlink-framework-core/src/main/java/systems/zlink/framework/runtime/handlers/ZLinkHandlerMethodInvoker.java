package systems.zlink.framework.runtime.handlers;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.ParameterizedType;
import java.lang.reflect.Proxy;
import java.lang.reflect.Type;
import java.lang.reflect.WildcardType;
import java.util.Arrays;
import java.util.List;
import java.util.Collection;
import java.util.Objects;
import java.util.ServiceLoader;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.errors.ZLinkConfigurationException;

public final class ZLinkHandlerMethodInvoker {
    private static final String CONTINUATION_CLASS_NAME = "kotlin.coroutines.Continuation";
    private static final List<ZLinkSuspendHandlerInvoker> SUSPEND_INVOKERS = ServiceLoader
        .load(ZLinkSuspendHandlerInvoker.class)
        .stream()
        .map(ServiceLoader.Provider::get)
        .toList();

    private ZLinkHandlerMethodInvoker() {
    }

    public static boolean isKotlinSuspendMethod(Method method) {
        Class<?>[] parameterTypes = method.getParameterTypes();
        return parameterTypes.length > 0
            && CONTINUATION_CLASS_NAME.equals(parameterTypes[parameterTypes.length - 1].getName());
    }

    public static Class<?>[] logicalParameterTypes(Method method) {
        Class<?>[] parameterTypes = method.getParameterTypes();
        return isKotlinSuspendMethod(method)
            ? Arrays.copyOf(parameterTypes, parameterTypes.length - 1)
            : parameterTypes;
    }

    public static Class<?> kotlinSuspendReplyType(Class<?> handlerType, Method method) {
        if (!isKotlinSuspendMethod(method)) {
            throw new IllegalArgumentException("method is not a Kotlin suspend method");
        }
        Type[] parameterTypes = method.getGenericParameterTypes();
        Type continuationType = parameterTypes[parameterTypes.length - 1];
        if (continuationType instanceof ParameterizedType parameterized
            && parameterized.getRawType() instanceof Class<?> raw
            && CONTINUATION_CLASS_NAME.equals(raw.getName())) {
            return requireContinuationResultType(handlerType, parameterized.getActualTypeArguments()[0]);
        }
        throw new ZLinkConfigurationException(
            "Kotlin suspend handler reply type must resolve from Continuation: "
                + handlerType.getName() + "." + method.getName());
    }

    public static CompletionStage<Object> invoke(Object handler, Method method, Object[] logicalArguments) {
        return invoke(handler, method, logicalArguments, SUSPEND_INVOKERS);
    }

    public static CompletionStage<Object> invokeHandler(
        Object handler,
        String methodName,
        Object[] logicalArguments,
        Collection<ZLinkSuspendHandlerInvoker> suspendInvokers) {
        Method method = requireHandlerMethod(handler.getClass(), methodName, logicalArguments);
        return invoke(handler, method, logicalArguments, suspendInvokers);
    }

    public static Method requireHandlerMethod(
        Class<?> handlerType,
        String methodName,
        Object[] logicalArguments) {
        Objects.requireNonNull(handlerType, "handlerType");
        Objects.requireNonNull(methodName, "methodName");
        Object[] args = logicalArguments == null ? new Object[0] : logicalArguments;
        Method fallback = null;
        for (Method method : handlerType.getMethods()) {
            if (!methodName.equals(method.getName())) {
                continue;
            }
            Class<?>[] parameterTypes = logicalParameterTypes(method);
            if (parameterTypes.length != args.length) {
                continue;
            }
            if (argumentsMatch(parameterTypes, args)) {
                return method;
            }
            if (fallback == null) {
                fallback = method;
            }
        }
        if (fallback != null) {
            return fallback;
        }
        throw new ZLinkConfigurationException(
            "handler method is not found: " + handlerType.getName() + "." + methodName);
    }

    public static CompletionStage<Object> invoke(
        Object handler,
        Method method,
        Object[] logicalArguments,
        Collection<ZLinkSuspendHandlerInvoker> suspendInvokers) {
        if (!isKotlinSuspendMethod(method)) {
            try {
                method.setAccessible(true);
                Object result = method.invoke(handler, logicalArguments);
                return CompletableFuture.completedFuture(result);
            } catch (IllegalAccessException | InvocationTargetException ex) {
                return CompletableFuture.failedFuture(unwrapReflectionFailure(ex));
            }
        }
        Collection<ZLinkSuspendHandlerInvoker> effectiveInvokers =
            suspendInvokers == null || suspendInvokers.isEmpty()
                ? SUSPEND_INVOKERS
                : suspendInvokers;
        for (ZLinkSuspendHandlerInvoker invoker : effectiveInvokers) {
            if (invoker.supports(method)) {
                return invoker.invoke(handler, method, logicalArguments);
            }
        }
        return invokeKotlinSuspend(handler, method, logicalArguments);
    }

    private static CompletionStage<Object> invokeKotlinSuspend(
        Object handler,
        Method method,
        Object[] logicalArguments) {
        CompletableFuture<Object> completion = new CompletableFuture<>();
        try {
            Object[] arguments = new Object[logicalArguments.length + 1];
            System.arraycopy(logicalArguments, 0, arguments, 0, logicalArguments.length);
            ClassLoader loader = kotlinClassLoader(method);
            arguments[arguments.length - 1] = newContinuation(loader, completion);
            method.setAccessible(true);
            Object result = method.invoke(handler, arguments);
            if (result != coroutineSuspendedMarker(loader)) {
                completeFromKotlinResult(loader, completion, result);
            }
        } catch (IllegalAccessException | InvocationTargetException ex) {
            completion.completeExceptionally(unwrapReflectionFailure(ex));
        } catch (RuntimeException ex) {
            completion.completeExceptionally(ex);
        }
        return completion;
    }

    private static boolean argumentsMatch(Class<?>[] parameterTypes, Object[] arguments) {
        for (int index = 0; index < parameterTypes.length; index++) {
            Object argument = arguments[index];
            if (argument == null) {
                continue;
            }
            Class<?> parameterType = wrapPrimitive(parameterTypes[index]);
            if (!parameterType.isInstance(argument)) {
                return false;
            }
        }
        return true;
    }

    private static Class<?> wrapPrimitive(Class<?> type) {
        if (!type.isPrimitive()) {
            return type;
        }
        if (type == int.class) {
            return Integer.class;
        }
        if (type == long.class) {
            return Long.class;
        }
        if (type == boolean.class) {
            return Boolean.class;
        }
        if (type == byte.class) {
            return Byte.class;
        }
        if (type == short.class) {
            return Short.class;
        }
        if (type == float.class) {
            return Float.class;
        }
        if (type == double.class) {
            return Double.class;
        }
        if (type == char.class) {
            return Character.class;
        }
        return Void.class;
    }

    private static Object newContinuation(ClassLoader loader, CompletableFuture<Object> completion) {
        try {
            Class<?> continuationType = Class.forName(CONTINUATION_CLASS_NAME, false, loader);
            InvocationHandler handler = (proxy, method, args) -> {
                if ("getContext".equals(method.getName()) && method.getParameterCount() == 0) {
                    return emptyCoroutineContext(loader);
                }
                if ("resumeWith".equals(method.getName()) && method.getParameterCount() == 1) {
                    completeFromKotlinResult(loader, completion, args[0]);
                    return null;
                }
                if ("toString".equals(method.getName()) && method.getParameterCount() == 0) {
                    return "ZLinkHandlerContinuation";
                }
                return null;
            };
            return Proxy.newProxyInstance(loader, new Class<?>[] {continuationType}, handler);
        } catch (ClassNotFoundException ex) {
            throw new ZLinkConfigurationException(
                "Kotlin suspend handler requires kotlin-stdlib on the runtime classpath",
                ex);
        }
    }

    private static Object emptyCoroutineContext(ClassLoader loader) {
        try {
            Class<?> contextType = Class.forName("kotlin.coroutines.EmptyCoroutineContext", false, loader);
            return contextType.getField("INSTANCE").get(null);
        } catch (ReflectiveOperationException ex) {
            throw new ZLinkConfigurationException(
                "Kotlin suspend handler requires EmptyCoroutineContext on the runtime classpath",
                ex);
        }
    }

    private static Object coroutineSuspendedMarker(ClassLoader loader) {
        try {
            Class<?> intrinsics =
                Class.forName("kotlin.coroutines.intrinsics.IntrinsicsKt", false, loader);
            Method method = intrinsics.getMethod("getCOROUTINE_SUSPENDED");
            method.setAccessible(true);
            return method.invoke(null);
        } catch (ReflectiveOperationException ex) {
            throw new ZLinkConfigurationException(
                "Kotlin suspend handler requires kotlin coroutine intrinsics on the runtime classpath",
                ex);
        }
    }

    private static void completeFromKotlinResult(
        ClassLoader loader,
        CompletableFuture<Object> completion,
        Object result) {
        try {
            Class<?> resultKt = Class.forName("kotlin.ResultKt", false, loader);
            Method throwOnFailure = resultKt.getMethod("throwOnFailure", Object.class);
            throwOnFailure.setAccessible(true);
            throwOnFailure.invoke(null, result);
            completion.complete(isKotlinUnit(result) ? null : result);
        } catch (InvocationTargetException ex) {
            completion.completeExceptionally(unwrapReflectionFailure(ex));
        } catch (ReflectiveOperationException ex) {
            completion.completeExceptionally(new ZLinkConfigurationException(
                "Kotlin suspend handler requires kotlin.ResultKt on the runtime classpath",
                ex));
        }
    }

    private static boolean isKotlinUnit(Object value) {
        return value != null && "kotlin.Unit".equals(value.getClass().getName());
    }

    private static Throwable unwrapReflectionFailure(Throwable throwable) {
        Throwable current = throwable;
        while (current instanceof InvocationTargetException invocation && invocation.getCause() != null) {
            current = invocation.getCause();
        }
        return current;
    }

    private static ClassLoader kotlinClassLoader(Method method) {
        ClassLoader loader = method.getDeclaringClass().getClassLoader();
        return loader == null ? Thread.currentThread().getContextClassLoader() : loader;
    }

    private static Class<?> requireContinuationResultType(Class<?> handlerType, Type argument) {
        if (argument instanceof WildcardType wildcard && wildcard.getLowerBounds().length == 1) {
            return requireContinuationResultType(handlerType, wildcard.getLowerBounds()[0]);
        }
        if (argument instanceof Class<?> klass) {
            return klass;
        }
        if (argument instanceof ParameterizedType parameterized
            && parameterized.getRawType() instanceof Class<?> raw) {
            return raw;
        }
        throw new ZLinkConfigurationException(
            "Kotlin suspend handler reply type must resolve to a class: " + handlerType.getName());
    }
}
