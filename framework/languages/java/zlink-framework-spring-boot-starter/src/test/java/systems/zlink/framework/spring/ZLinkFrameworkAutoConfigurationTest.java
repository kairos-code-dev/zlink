package systems.zlink.framework.spring;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.List;
import org.junit.jupiter.api.Test;
import org.springframework.context.annotation.AnnotationConfigApplicationContext;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterFactory;
import systems.zlink.framework.testkit.FakeZLinkBackendAdapterFactory;

final class ZLinkFrameworkAutoConfigurationTest {
    @Test
    void autoConfigurationStartsFrameworkLifecycleAndExposesClientBean() {
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(
                ZLinkBackendAdapterFactory.class,
                FakeZLinkBackendAdapterFactory::new);
            context.register(TestConfig.class, ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            ZLinkFrameworkLifecycle lifecycle =
                context.getBean(ZLinkFrameworkLifecycle.class);
            ZLinkClient client = context.getBean(ZLinkClient.class);

            assertTrue(lifecycle.isRunning());
            assertInstanceOf(ZLinkFrameworkLifecycle.class, client);
        }
    }

    @Test
    void autoConfigurationAppliesCustomizersBeforeRuntimeStarts() {
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(ZLinkBackendAdapterFactory.class, () -> backendFactory);
            context.register(TestConfig.class, ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            assertEquals(
                List.of(
                    "factory.channel",
                    "create.context",
                    "create.dealer",
                    "dealer.connect.inproc://profile-server"),
                backendFactory.calls());
        }

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

    @Configuration
    static class TestConfig {
        @Bean
        ZLinkFrameworkOptionsCustomizer profileChannelCustomizer() {
            return options -> options.addClientServerChannel("profile", channel ->
                channel.enableClient(client ->
                    client.useManualConnections(endpoints ->
                        endpoints.connect("inproc://profile-server"))));
        }
    }
}
