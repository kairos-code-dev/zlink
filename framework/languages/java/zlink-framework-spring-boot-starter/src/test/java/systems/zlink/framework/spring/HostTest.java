package systems.zlink.framework.spring;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.List;
import org.junit.jupiter.api.Test;
import org.springframework.context.annotation.AnnotationConfigApplicationContext;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions;
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
                "dealer.connect.inproc://profile-server",
                "close.dealer",
                "close.context",
                "close.registry",
                "close.context"),
            backendFactory.calls());
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
}
