package systems.zlink.framework.spring;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.List;
import java.time.Duration;
import org.junit.jupiter.api.Test;
import org.springframework.context.annotation.AnnotationConfigApplicationContext;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendAdapterProvider;
import systems.zlink.framework.runtime.host.ZLinkFrameworkLifecycle;
import systems.zlink.framework.testkit.FakeZLinkBackendAdapterFactory;

final class HostTest {
    @Test
    void springShutdownUsesFrameworkTerminationDeadline() throws Exception {
        var field = ZLinkFrameworkLifecycle.class
            .getDeclaredField("SPRING_SHUTDOWN_DRAIN_DEADLINE");
        field.setAccessible(true);

        assertEquals(Duration.ofSeconds(30), field.get(null));
    }

    @Test
    void host_startsAndStops_frameworkRuntimeContext() {
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();
        ZLinkFrameworkLifecycle lifecycle;

        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(ZLinkBackendAdapterProvider.class, () -> backendFactory);
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

    @Configuration
    @EnableZLinkFramework
    static class ProfileChannelConfig {
        @Bean
        ZLinkFrameworkConfigurer profileChannelConfigurer() {
            return options -> { var channel = options.addClientServerChannel("profile"); channel.enableClient("inproc://profile-server"); };
        }
    }

}
