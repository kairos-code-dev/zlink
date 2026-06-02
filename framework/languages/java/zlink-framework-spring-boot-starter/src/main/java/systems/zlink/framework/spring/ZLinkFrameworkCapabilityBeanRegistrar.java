package systems.zlink.framework.spring;

import org.springframework.beans.BeansException;
import org.springframework.beans.factory.config.BeanFactoryPostProcessor;
import org.springframework.beans.factory.config.ConfigurableListableBeanFactory;
import org.springframework.beans.factory.support.AbstractBeanDefinition;
import org.springframework.beans.factory.support.BeanDefinitionRegistry;
import org.springframework.beans.factory.support.RootBeanDefinition;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.framework.spots.ZLinkSpotOutbound;
import systems.zlink.framework.spots.ZLinkSpotPublisherClient;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddressResolver;

final class ZLinkFrameworkCapabilityBeanRegistrar implements BeanFactoryPostProcessor {
    private static final String SPOT_MANAGER_BEAN_NAME = "zlinkSpotManager";
    private static final String SPOT_OUTBOUND_BEAN_NAME = "zlinkSpotOutbound";
    private static final String SPOT_PUBLISHER_CLIENT_BEAN_NAME =
        "zlinkSpotPublisherClient";
    private static final String SPOT_REMOTE_ADDRESS_RESOLVER_BEAN_NAME =
        "zlinkSpotRemoteAddressResolver";
    private static final String ACTOR_MANAGER_BEAN_NAME = "zlinkActorManager";

    @Override
    public void postProcessBeanFactory(ConfigurableListableBeanFactory beanFactory)
        throws BeansException {
        if (!(beanFactory instanceof BeanDefinitionRegistry registry)) {
            return;
        }

        DefaultZLinkFrameworkOptions options =
            beanFactory.getBean(DefaultZLinkFrameworkOptions.class);
        options.validate();

        boolean hasSpotNode = !options.registration().spotNodes().isEmpty();
        boolean hasActorFactory = !options.registration().actorFactories().isEmpty();
        boolean hasSpotPublisherClient = options.registration().spotNodes().stream()
            .anyMatch(node -> !node.attachedSpotPublisherClients().isEmpty());
        boolean hasRegistrySpotRemoteAddresses =
            options.registration().registrySpotRemoteAddresses() != null;

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
        if (!hasBean(beanFactory, ZLinkSpotRemoteAddressResolver.class)) {
            if (options.registration().spotRemoteAddressResolverType() != null) {
                registerDelegate(
                    registry,
                    SPOT_REMOTE_ADDRESS_RESOLVER_BEAN_NAME,
                    options.registration().spotRemoteAddressResolverType());
            } else if (hasRegistrySpotRemoteAddresses) {
                registerDelegate(
                    registry,
                    SPOT_REMOTE_ADDRESS_RESOLVER_BEAN_NAME,
                    ZLinkFrameworkRegistrySpotRemoteAddressResolverBean.class);
            }
        }
        if (hasSpotNode && hasActorFactory && !hasBean(beanFactory, ZLinkActorManager.class)) {
            registerDelegate(registry, ACTOR_MANAGER_BEAN_NAME, ZLinkFrameworkActorManagerBean.class);
        }
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
