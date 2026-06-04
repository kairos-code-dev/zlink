package systems.zlink.framework.spring;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.List;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.BeanCreationException;
import org.springframework.beans.factory.NoSuchBeanDefinitionException;
import org.springframework.context.annotation.AnnotationConfigApplicationContext;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions;
import systems.zlink.framework.registry.ZLinkRegistryQuery;
import systems.zlink.framework.registry.ZLinkRegistryQueryClient;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterFactory;
import systems.zlink.framework.testkit.FakeZLinkBackendAdapterFactory;

final class HostTest {
    @Test
    void host_startsAndStops_frameworkRuntimeContext() {
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();
        ZLinkFrameworkLifecycle lifecycle;

        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(ZLinkBackendAdapterFactory.class, () -> backendFactory);
            context.register(ProfileChannelConfig.class, ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            lifecycle = context.getBean(ZLinkFrameworkLifecycle.class);

            assertTrue(lifecycle.isRunning());
        }

        assertFalse(lifecycle.isRunning());
        assertEquals(
            List.of(
                "factory.channel",
                "create.context",
                "create.dealer",
                "dealer.setChannelName.profile",
                "dealer.connect.inproc://profile-server",
                "close.dealer",
                "close.context"),
            backendFactory.calls());
    }

    @Test
    void host_startsEmbeddedRegistry_beforeFrameworkRuntime() {
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(ZLinkBackendAdapterFactory.class, () -> backendFactory);
            context.register(
                EmbeddedRegistryConfig.class,
                ProfileChannelConfig.class,
                ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            assertTrue(context.getBean(ZLinkRegistryLifecycle.class).isRunning());
            assertTrue(context.getBean(ZLinkFrameworkLifecycle.class).isRunning());
            assertInstanceOf(
                ZLinkRegistryLifecycle.class,
                context.getBean(ZLinkRegistryQuery.class));
            assertEquals(
                List.of(
                    "factory.channel",
                    "factory.registry",
                    "create.context",
                    "create.registry",
                    "registry.bind.inproc://registry-pub.inproc://registry-router",
                    "factory.channel",
                    "create.context",
                    "create.dealer",
                    "dealer.setChannelName.profile",
                    "dealer.connect.inproc://profile-server"),
                backendFactory.calls());
        }

        assertEquals(
                List.of(
                    "factory.channel",
                    "factory.registry",
                    "create.context",
                    "create.registry",
                    "registry.bind.inproc://registry-pub.inproc://registry-router",
                "factory.channel",
                "create.context",
                "create.dealer",
                "dealer.setChannelName.profile",
                "dealer.connect.inproc://profile-server",
                "close.dealer",
                "close.context",
                "close.registry",
                "close.context"),
            backendFactory.calls());
    }

    @Test
    void host_doesNotRegisterRegistryQuery_withoutEmbeddedRegistry() {
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(
                ZLinkBackendAdapterFactory.class,
                FakeZLinkBackendAdapterFactory::new);
            context.register(ProfileChannelConfig.class, ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            assertThrows(NoSuchBeanDefinitionException.class, () ->
                context.getBean(ZLinkRegistryQuery.class));
        }
    }

    @Test
    void host_doesNotRegisterRegistryQueryClient_withoutCustomizer() {
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(
                ZLinkBackendAdapterFactory.class,
                FakeZLinkBackendAdapterFactory::new);
            context.register(ProfileChannelConfig.class, ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            assertThrows(NoSuchBeanDefinitionException.class, () ->
                context.getBean(ZLinkRegistryQueryClient.class));
        }
    }

    @Test
    void host_registersRegistryQueryClient_whenCustomizerExists() {
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(ZLinkBackendAdapterFactory.class, () -> backendFactory);
            context.register(
                RegistryQueryClientConfig.class,
                ProfileChannelConfig.class,
                ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            assertInstanceOf(
                ZLinkRegistryQueryClient.class,
                context.getBean(ZLinkRegistryQueryClient.class));
            assertTrue(backendFactory.calls().contains(
                "registryQueryClient.connect.inproc://registry-router"));
        }
    }

    @Test
    void host_rejectsRegistryQueryClientCustomizer_withoutEndpoint() {
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(
                ZLinkBackendAdapterFactory.class,
                FakeZLinkBackendAdapterFactory::new);
            context.register(
                InvalidRegistryQueryClientConfig.class,
                ZLinkFrameworkAutoConfiguration.class);

            BeanCreationException exception =
                assertThrows(BeanCreationException.class, context::refresh);
            assertInstanceOf(
                systems.zlink.framework.errors.ZLinkConfigurationException.class,
                exception.getMostSpecificCause());
        }
    }

    @Configuration
    static class ProfileChannelConfig {
        @Bean
        ZLinkFrameworkOptionsCustomizer profileChannelCustomizer() {
            return options -> options.addClientServerChannel("profile", channel ->
                channel.enableClient(client ->
                    client.useManualConnections(endpoints ->
                        endpoints.connect("inproc://profile-server"))));
        }
    }

    @Configuration
    static class EmbeddedRegistryConfig {
        @Bean
        ZLinkEmbeddedRegistryOptions zlinkEmbeddedRegistryOptions() {
            ZLinkEmbeddedRegistryOptions options = new ZLinkEmbeddedRegistryOptions();
            options.setPubEndpoint("inproc://registry-pub");
            options.setRouterEndpoint("inproc://registry-router");
            return options;
        }
    }

    @Configuration
    static class RegistryQueryClientConfig {
        @Bean
        ZLinkRegistryQueryClientCustomizer registryQueryClientCustomizer() {
            return options -> options.setEndpoint("inproc://registry-router");
        }
    }

    @Configuration
    static class InvalidRegistryQueryClientConfig {
        @Bean
        ZLinkRegistryQueryClientCustomizer invalidRegistryQueryClientCustomizer() {
            return options -> {
            };
        }
    }
}
