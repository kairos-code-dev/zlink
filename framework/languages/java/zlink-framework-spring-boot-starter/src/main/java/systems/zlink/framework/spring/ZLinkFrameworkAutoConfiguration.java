package systems.zlink.framework.spring;

import java.util.List;
import org.springframework.beans.factory.ObjectProvider;
import org.springframework.beans.factory.config.BeanFactoryPostProcessor;
import org.springframework.beans.factory.config.AutowireCapableBeanFactory;
import org.springframework.boot.autoconfigure.AutoConfiguration;
import org.springframework.boot.autoconfigure.condition.ConditionalOnBean;
import org.springframework.boot.autoconfigure.condition.ConditionalOnMissingBean;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.channels.ZLinkChannelRuntimeOptions;
import systems.zlink.framework.channels.ZLinkFanoutClient;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.locations.ZLinkLocationStore;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventHandler;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterFactory;
import systems.zlink.framework.runtime.binding.ZLinkJavaBackendAdapterFactory;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;
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
    public ZLinkBackendAdapterFactory zlinkBackendAdapterFactory() {
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
    public ZLinkHandlerFactory zlinkHandlerFactory(AutowireCapableBeanFactory beanFactory) {
        return new ZLinkSpringHandlerFactory(beanFactory);
    }

    @Bean
    @ConditionalOnBean(DefaultZLinkFrameworkOptions.class)
    @ConditionalOnMissingBean
    public ZLinkFrameworkLifecycle zlinkFrameworkLifecycle(
        DefaultZLinkFrameworkOptions options,
        ZLinkBackendAdapterFactory backendAdapterFactory,
        ZLinkHandlerFactory handlerFactory,
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
        ZLinkBackendAdapterFactory backendAdapterFactory,
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

}
