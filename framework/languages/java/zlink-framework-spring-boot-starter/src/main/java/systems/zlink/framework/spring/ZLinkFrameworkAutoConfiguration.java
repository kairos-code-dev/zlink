package systems.zlink.framework.spring;

import java.util.List;
import org.springframework.beans.factory.ObjectProvider;
import org.springframework.beans.factory.config.BeanFactoryPostProcessor;
import org.springframework.beans.factory.config.AutowireCapableBeanFactory;
import org.springframework.boot.autoconfigure.AutoConfiguration;
import org.springframework.boot.autoconfigure.condition.ConditionalOnBean;
import org.springframework.boot.autoconfigure.condition.ConditionalOnMissingBean;
import org.springframework.boot.autoconfigure.condition.ConditionalOnClass;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.channels.ZLinkChannelRuntimeOptions;
import systems.zlink.framework.channels.ZLinkFanoutClient;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.locations.ZLinkLocationStore;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventHandler;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendAdapterProvider;
import systems.zlink.framework.runtime.binding.ZLinkJavaBackendAdapterFactory;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerActivator;
import systems.zlink.framework.runtime.host.ZLinkFrameworkLifecycle;
import systems.zlink.framework.runtime.monitoring.DefaultZLinkMonitoringOptions;

@AutoConfiguration
public class ZLinkFrameworkAutoConfiguration {
    @Bean
    @ConditionalOnBean(ZLinkFrameworkEnabled.class)
    public static BeanFactoryPostProcessor zlinkFrameworkCapabilityBeanRegistrar() {
        return new ZLinkFrameworkCapabilityBeanRegistrar();
    }

    @Bean
    @ConditionalOnBean(ZLinkFrameworkEnabled.class)
    @ConditionalOnMissingBean
    public DefaultZLinkFrameworkOptions zlinkFrameworkOptions(
        List<ZLinkFrameworkConfigurer> configurers) {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        for (ZLinkFrameworkConfigurer configurer : configurers) {
            configurer.configure(options);
        }
        return options;
    }

    @Bean
    @ConditionalOnMissingBean
    public ZLinkBackendAdapterProvider zlinkBackendAdapterFactory() {
        return new ZLinkJavaBackendAdapterFactory();
    }

    @Bean
    @ConditionalOnBean(ZLinkFrameworkEnabled.class)
    public ZLinkFrameworkConfigurer zlinkLocationStoreConfigurer(
        ObjectProvider<ZLinkLocationStore> locationStore) {
        return options -> {
            ZLinkLocationStore unified = locationStore.getIfUnique();
            if (unified != null) {
                options.addLocationStore(unified);
            }
        };
    }

    @Bean
    @ConditionalOnMissingBean
    public ZLinkRuntimeEventDispatcher zlinkRuntimeEventDispatcher() {
        return new ZLinkRuntimeEventDispatcher();
    }

    @Bean
    @ConditionalOnMissingBean
    public DefaultZLinkMonitoringOptions zlinkMonitoringOptions(
        List<ZLinkMonitoringOptionsCustomizer> customizers) {
        DefaultZLinkMonitoringOptions options = new DefaultZLinkMonitoringOptions();
        for (ZLinkMonitoringOptionsCustomizer customizer : customizers) {
            customizer.customize(options);
        }
        return options;
    }

    @Bean
    @ConditionalOnBean(DefaultZLinkFrameworkOptions.class)
    @ConditionalOnMissingBean
    public ZLinkHandlerActivator zlinkHandlerFactory(AutowireCapableBeanFactory beanFactory) {
        return new ZLinkSpringHandlerFactory(beanFactory);
    }

    @Bean
    @ConditionalOnBean(DefaultZLinkFrameworkOptions.class)
    @ConditionalOnMissingBean
    public ZLinkFrameworkLifecycle zlinkFrameworkLifecycle(
        DefaultZLinkFrameworkOptions options,
        ZLinkBackendAdapterProvider backendAdapterFactory,
        ZLinkHandlerActivator handlerFactory,
        ZLinkRuntimeEventDispatcher dispatcher) {
        return new ZLinkFrameworkLifecycle(
            options,
            backendAdapterFactory,
            handlerFactory,
            dispatcher);
    }

    @Bean
    @ConditionalOnBean(DefaultZLinkMonitoringOptions.class)
    @ConditionalOnMissingBean
    public ZLinkMonitoringLifecycle zlinkMonitoringLifecycle(
        DefaultZLinkMonitoringOptions options,
        ZLinkBackendAdapterProvider backendAdapterFactory,
        ZLinkRuntimeEventDispatcher dispatcher,
        ObjectProvider<ZLinkFrameworkLifecycle> frameworkLifecycle,
        ObjectProvider<ZLinkRuntimeEventHandler<?>> eventHandlers) {
        return new ZLinkMonitoringLifecycle(
            options,
            backendAdapterFactory,
            dispatcher,
            frameworkLifecycle,
            eventHandlers.orderedStream().toList());
    }

    @Bean
    @ConditionalOnBean(ZLinkFrameworkLifecycle.class)
    @ConditionalOnMissingBean
    public ZLinkClient zlinkClient(ZLinkFrameworkLifecycle lifecycle) {
        return lifecycle;
    }

    @Bean
    @ConditionalOnBean(ZLinkFrameworkLifecycle.class)
    @ConditionalOnMissingBean
    public ZLinkChannelRuntimeOptions zlinkChannelRuntimeOptions(
        ZLinkFrameworkLifecycle lifecycle) {
        return lifecycle;
    }

    @Bean
    @ConditionalOnBean(ZLinkFrameworkLifecycle.class)
    @ConditionalOnMissingBean
    public ZLinkFanoutClient zlinkFanoutClient(ZLinkFrameworkLifecycle lifecycle) {
        return lifecycle;
    }

    @Bean
    @ConditionalOnBean(ZLinkFrameworkLifecycle.class)
    @ConditionalOnMissingBean
    public ZLinkRouteClient zlinkRouteClient(ZLinkFrameworkLifecycle lifecycle) {
        return lifecycle;
    }

    @Bean
    @ConditionalOnBean(ZLinkFrameworkLifecycle.class)
    @ConditionalOnMissingBean
    public systems.zlink.framework.spots.SpotHandleResolver zlinkSpotHandleResolver(
        ZLinkFrameworkLifecycle lifecycle) {
        return spotRid -> lifecycle.spotHandleResolver().resolveSpotHandle(spotRid);
    }

    @Bean
    @ConditionalOnBean(ZLinkFrameworkLifecycle.class)
    @ConditionalOnMissingBean
    public systems.zlink.framework.spots.ActorSpotHandleResolver zlinkActorSpotHandleResolver(
        ZLinkFrameworkLifecycle lifecycle) {
        return actorId -> lifecycle.actorSpotHandleResolver().resolveActorSpotHandle(actorId);
    }

    @Bean
    @ConditionalOnBean(ZLinkFrameworkLifecycle.class)
    @ConditionalOnMissingBean
    public systems.zlink.framework.monitoring.ZLinkDrainControl zlinkDrainControl(
        ZLinkFrameworkLifecycle lifecycle) {
        return lifecycle;
    }

    @Bean("zlinkDrainReadiness")
    @ConditionalOnClass(name = "org.springframework.boot.actuate.health.HealthIndicator")
    @ConditionalOnBean(systems.zlink.framework.monitoring.ZLinkDrainControl.class)
    @ConditionalOnMissingBean(name = "zlinkDrainReadiness")
    public ZLinkDrainReadinessContributor zlinkDrainReadinessContributor(
        systems.zlink.framework.monitoring.ZLinkDrainControl drainControl) {
        return new ZLinkDrainReadinessContributor(drainControl);
    }

}
