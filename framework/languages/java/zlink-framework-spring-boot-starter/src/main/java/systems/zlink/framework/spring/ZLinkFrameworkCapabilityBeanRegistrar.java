package systems.zlink.framework.spring;

import java.beans.Introspector;
import java.lang.reflect.ParameterizedType;
import java.lang.reflect.Type;
import java.util.Collection;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;
import org.springframework.beans.BeansException;
import org.springframework.beans.factory.config.BeanDefinition;
import org.springframework.beans.factory.config.BeanFactoryPostProcessor;
import org.springframework.beans.factory.config.ConfigurableListableBeanFactory;
import org.springframework.beans.factory.support.AbstractBeanDefinition;
import org.springframework.beans.factory.support.BeanDefinitionRegistry;
import org.springframework.beans.factory.support.RootBeanDefinition;
import org.springframework.context.annotation.ClassPathScanningCandidateComponentProvider;
import org.springframework.core.type.filter.AssignableTypeFilter;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.streams.StreamNodeRegistration;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.framework.spots.ZLinkSpotOutbound;
import systems.zlink.framework.spots.ZLinkSpotPublisherClient;
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher;
import systems.zlink.framework.streams.ZLinkSessionPacketHandler;

final class ZLinkFrameworkCapabilityBeanRegistrar implements BeanFactoryPostProcessor {
    private static final String SPOT_MANAGER_BEAN_NAME = "zlinkSpotManager";
    private static final String SPOT_OUTBOUND_BEAN_NAME = "zlinkSpotOutbound";
    private static final String SPOT_PUBLISHER_CLIENT_BEAN_NAME =
        "zlinkSpotPublisherClient";
    private static final String ACTOR_MANAGER_BEAN_NAME = "zlinkActorManager";

    @Override
    public void postProcessBeanFactory(ConfigurableListableBeanFactory beanFactory)
        throws BeansException {
        if (!(beanFactory instanceof BeanDefinitionRegistry registry)) {
            return;
        }

        DefaultZLinkFrameworkOptions options =
            beanFactory.getBean(DefaultZLinkFrameworkOptions.class);
        discoverSessionPacketHandlers(options);
        options.validate();
        registerApplicationBeans(registry, beanFactory, options);

        boolean hasSpotNode = !options.registration().spotNodes().isEmpty();
        boolean hasActorFactory = options.registration().spotNodes().stream()
            .anyMatch(node -> !node.actorFactories().isEmpty());
        boolean hasSpotPublisherClient = options.registration().spotNodes().stream()
            .anyMatch(node -> node.pubSubEnabled());

        if (hasSpotNode && !hasBean(beanFactory, ZLinkSpotManager.class)) {
            registerDelegate(registry, SPOT_MANAGER_BEAN_NAME, ZLinkFrameworkSpotManagerBean.class);
        }
        if (hasSpotNode && !hasBean(beanFactory, ZLinkSpotOutbound.class)) {
            registerDelegate(registry, SPOT_OUTBOUND_BEAN_NAME, ZLinkFrameworkSpotOutboundBean.class);
        }
        if (hasSpotPublisherClient && !hasBean(beanFactory, ZLinkSpotPublisherClient.class)) {
            registerDelegate(
                registry,
                SPOT_PUBLISHER_CLIENT_BEAN_NAME,
                ZLinkFrameworkSpotPublisherClientBean.class);
        }
        if (hasSpotNode && hasActorFactory && !hasBean(beanFactory, ZLinkActorManager.class)) {
            registerDelegate(registry, ACTOR_MANAGER_BEAN_NAME, ZLinkFrameworkActorManagerBean.class);
        }
    }

    private static void registerApplicationBeans(
        BeanDefinitionRegistry registry,
        ConfigurableListableBeanFactory beanFactory,
        DefaultZLinkFrameworkOptions options) {
        Set<Class<?>> applicationTypes = new LinkedHashSet<>(options.registration().applicationTypes());
        for (Class<?> applicationType : List.copyOf(applicationTypes)) {
            applicationTypes.addAll(findCollectionDependencyImplementations(applicationType));
        }
        for (Class<?> applicationType : applicationTypes) {
            registerPrototypeIfMissing(registry, beanFactory, applicationType);
        }
    }

    private static Set<Class<?>> findCollectionDependencyImplementations(Class<?> applicationType) {
        Set<Class<?>> implementations = new LinkedHashSet<>();
        for (var constructor : applicationType.getConstructors()) {
            for (Type parameter : constructor.getGenericParameterTypes()) {
                Class<?> serviceType = collectionElementType(parameter);
                if (serviceType == null || !serviceType.isInterface()) {
                    continue;
                }
                implementations.addAll(findAssignableTypes(applicationType.getPackageName(), serviceType));
                implementations.addAll(findAssignableTypes(serviceType.getPackageName(), serviceType));
            }
        }
        implementations.remove(applicationType);
        return implementations;
    }

    private static void discoverSessionPacketHandlers(DefaultZLinkFrameworkOptions options) {
        for (StreamNodeRegistration streamNode : options.registration().streamNodes()) {
            Class<?> sessionType = streamNode.sessionType();
            if (sessionType == null) {
                continue;
            }
            for (Class<?> contextType : findSessionPacketDispatcherContexts(sessionType)) {
                for (Class<? extends ZLinkSessionPacketHandler<?>> handlerType :
                     findSessionPacketHandlers(sessionType.getPackageName(), contextType)) {
                    streamNode.addSessionPacketHandler(handlerType);
                }
                for (Class<? extends ZLinkSessionPacketHandler<?>> handlerType :
                     findSessionPacketHandlers(contextType.getPackageName(), contextType)) {
                    streamNode.addSessionPacketHandler(handlerType);
                }
            }
        }
    }

    private static Set<Class<?>> findSessionPacketDispatcherContexts(Class<?> sessionType) {
        Set<Class<?>> contextTypes = new LinkedHashSet<>();
        for (var constructor : sessionType.getConstructors()) {
            for (Type parameter : constructor.getGenericParameterTypes()) {
                Class<?> contextType = sessionPacketDispatcherContextType(parameter);
                if (contextType != null) {
                    contextTypes.add(contextType);
                }
            }
        }
        return contextTypes;
    }

    private static Class<?> sessionPacketDispatcherContextType(Type parameter) {
        if (!(parameter instanceof ParameterizedType parameterized)
            || parameterized.getRawType() != ZLinkSessionPacketDispatcher.class) {
            return null;
        }
        Type argument = parameterized.getActualTypeArguments()[0];
        return argument instanceof Class<?> contextType ? contextType : null;
    }

    @SuppressWarnings("unchecked")
    private static Set<Class<? extends ZLinkSessionPacketHandler<?>>>
    findSessionPacketHandlers(String packageName, Class<?> contextType) {
        Set<Class<? extends ZLinkSessionPacketHandler<?>>> handlers = new LinkedHashSet<>();
        for (Class<?> type : findAssignableTypes(packageName, ZLinkSessionPacketHandler.class)) {
            if (sessionPacketHandlerContextType(type) == contextType) {
                handlers.add((Class<? extends ZLinkSessionPacketHandler<?>>) type);
            }
        }
        return handlers;
    }

    private static Class<?> sessionPacketHandlerContextType(Class<?> handlerType) {
        for (Type type : handlerType.getGenericInterfaces()) {
            Class<?> contextType = sessionPacketHandlerContextType(type);
            if (contextType != null) {
                return contextType;
            }
        }
        Class<?> superclass = handlerType.getSuperclass();
        return superclass == null ? null : sessionPacketHandlerContextType(superclass);
    }

    private static Class<?> sessionPacketHandlerContextType(Type type) {
        if (!(type instanceof ParameterizedType parameterized)
            || parameterized.getRawType() != ZLinkSessionPacketHandler.class) {
            return null;
        }
        Type argument = parameterized.getActualTypeArguments()[0];
        return argument instanceof Class<?> contextType ? contextType : null;
    }

    private static Class<?> collectionElementType(Type parameter) {
        if (!(parameter instanceof ParameterizedType parameterized)
            || !(parameterized.getRawType() instanceof Class<?> rawType)
            || !(rawType == List.class
                || rawType == Collection.class
                || rawType == Iterable.class
                || rawType == Set.class)) {
            return null;
        }
        Type argument = parameterized.getActualTypeArguments()[0];
        return argument instanceof Class<?> elementType ? elementType : null;
    }

    private static Set<Class<?>> findAssignableTypes(String packageName, Class<?> serviceType) {
        ClassPathScanningCandidateComponentProvider scanner =
            new ClassPathScanningCandidateComponentProvider(false);
        scanner.addIncludeFilter(new AssignableTypeFilter(serviceType));
        Set<Class<?>> types = new LinkedHashSet<>();
        for (BeanDefinition candidate : scanner.findCandidateComponents(packageName)) {
            String className = candidate.getBeanClassName();
            if (className == null) {
                continue;
            }
            try {
                Class<?> type = Class.forName(className, false, contextClassLoader(serviceType));
                if (type != serviceType && serviceType.isAssignableFrom(type)) {
                    types.add(type);
                }
            } catch (ClassNotFoundException ignored) {
                // Optional package contents should not make framework registration unusable.
            }
        }
        return types;
    }

    private static ClassLoader contextClassLoader(Class<?> fallbackType) {
        ClassLoader loader = Thread.currentThread().getContextClassLoader();
        return loader == null ? fallbackType.getClassLoader() : loader;
    }

    private static void registerPrototypeIfMissing(
        BeanDefinitionRegistry registry,
        ConfigurableListableBeanFactory beanFactory,
        Class<?> beanClass) {
        if (hasBean(beanFactory, beanClass)) {
            return;
        }
        RootBeanDefinition definition = new RootBeanDefinition(beanClass);
        definition.setAutowireMode(AbstractBeanDefinition.AUTOWIRE_CONSTRUCTOR);
        definition.setScope(BeanDefinition.SCOPE_PROTOTYPE);
        registry.registerBeanDefinition(
            "zlinkApplication." + Introspector.decapitalize(beanClass.getName()),
            definition);
    }

    private static boolean hasBean(
        ConfigurableListableBeanFactory beanFactory,
        Class<?> beanType) {
        return beanFactory.getBeanNamesForType(beanType, true, false).length > 0;
    }

    private static void registerDelegate(
        BeanDefinitionRegistry registry,
        String beanName,
        Class<?> beanClass) {
        if (registry.containsBeanDefinition(beanName)) {
            return;
        }
        RootBeanDefinition definition = new RootBeanDefinition(beanClass);
        definition.setAutowireMode(AbstractBeanDefinition.AUTOWIRE_CONSTRUCTOR);
        registry.registerBeanDefinition(beanName, definition);
    }
}
