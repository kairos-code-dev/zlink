package systems.zlink.framework.runtime.handlers;

import java.lang.reflect.Constructor;
import java.util.LinkedHashMap;
import java.util.Map;
import systems.zlink.framework.errors.ZLinkConfigurationException;

@FunctionalInterface
public interface ZLinkHandlerFactory {
    Object create(Class<?> handlerType);

    static ZLinkHandlerFactory reflection() {
        return handlerType -> {
            try {
                return handlerType.getConstructor().newInstance();
            } catch (ReflectiveOperationException ex) {
                throw new ZLinkConfigurationException(
                    "failed to create handler: " + handlerType.getName(),
                    ex);
            }
        };
    }

    static MutableServices services() {
        return new MutableServices(reflection());
    }

    static MutableServices services(ZLinkHandlerFactory fallback) {
        return new MutableServices(fallback);
    }

    final class MutableServices implements ZLinkHandlerFactory {
        private final Map<Class<?>, Object> services = new LinkedHashMap<>();
        private final ZLinkHandlerFactory fallback;

        private MutableServices(ZLinkHandlerFactory fallback) {
            this.fallback = fallback;
        }

        public MutableServices add(Class<?> serviceType, Object service) {
            services.put(serviceType, service);
            return this;
        }

        @Override
        public Object create(Class<?> handlerType) {
            try {
                for (Constructor<?> constructor : handlerType.getConstructors()) {
                    Object[] arguments = resolveArguments(constructor.getParameterTypes());
                    if (arguments != null && arguments.length > 0) {
                        return constructor.newInstance(arguments);
                    }
                }
                return fallback.create(handlerType);
            } catch (ReflectiveOperationException ex) {
                throw new ZLinkConfigurationException(
                    "failed to create handler: " + handlerType.getName(),
                    ex);
            }
        }

        private Object[] resolveArguments(Class<?>[] parameterTypes) {
            Object[] arguments = new Object[parameterTypes.length];
            for (int i = 0; i < parameterTypes.length; i++) {
                Object service = findService(parameterTypes[i]);
                if (service == null) {
                    return null;
                }
                arguments[i] = service;
            }
            return arguments;
        }

        private Object findService(Class<?> parameterType) {
            for (Map.Entry<Class<?>, Object> entry : services.entrySet()) {
                if (parameterType.isAssignableFrom(entry.getKey())) {
                    return entry.getValue();
                }
            }
            if (fallback instanceof MutableServices parentServices) {
                return parentServices.findService(parameterType);
            }
            return null;
        }
    }
}
