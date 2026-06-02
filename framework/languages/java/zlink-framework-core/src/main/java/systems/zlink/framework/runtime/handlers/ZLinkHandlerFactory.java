package systems.zlink.framework.runtime.handlers;

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
                    "failed to create handler: " + handlerType.getName());
            }
        };
    }
}
