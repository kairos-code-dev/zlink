package systems.zlink.e2e.runtimemonitoring.trigger;

import java.time.Duration;
import org.springframework.boot.SpringBootConfiguration;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.EnableAutoConfiguration;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.e2e.runtimemonitoring.shared.Contracts;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkMonitoringOptionsCustomizer;

public final class Program {
    private Program() {
    }

    public static void main(String... args) {
        expectFailure(BadIntervalConfig.class, "registry interval must be positive");
        expectFailure(MissingSocketSourceConfig.class, "monitoring socket source is not configured");
        expectFailure(MissingSpotSourceConfig.class, "monitoring spot source is not configured");
        System.out.println("scenario MON-B2 passed");
    }

    private static void expectFailure(Class<?> configClass, String messagePart) {
        try (var context = new SpringApplicationBuilder(configClass)
                 .web(WebApplicationType.NONE)
                 .run()) {
            throw new IllegalStateException(
                configClass.getSimpleName() + " unexpectedly started");
        } catch (Throwable error) {
            if (!hasMessage(error, messagePart)) {
                throw new IllegalStateException(
                    configClass.getSimpleName() + " failed with unexpected error", error);
            }
        }
    }

    private static boolean hasMessage(Throwable error, String messagePart) {
        Throwable current = error;
        while (current != null) {
            String message = current.getMessage();
            if (message != null && message.contains(messagePart)) {
                return true;
            }
            current = current.getCause();
        }
        return false;
    }

    @SpringBootConfiguration(proxyBeanMethods = false)
    @EnableAutoConfiguration
    static class BadIntervalConfig {
        @Bean
        ZLinkMonitoringOptionsCustomizer badIntervalMonitoring() {
            return options -> options.addRegistryEvents(Contracts.REGISTRY_SOURCE, Duration.ZERO);
        }
    }

    @EnableZLinkFramework
    @SpringBootConfiguration(proxyBeanMethods = false)
    @EnableAutoConfiguration
    static class MissingSocketSourceConfig {
        @Bean
        ZLinkMonitoringOptionsCustomizer missingSocketMonitoring() {
            return options -> options.addSocketEvents("missing.socket");
        }
    }

    @EnableZLinkFramework
    @SpringBootConfiguration(proxyBeanMethods = false)
    @EnableAutoConfiguration
    static class MissingSpotSourceConfig {
        @Bean
        ZLinkMonitoringOptionsCustomizer missingSpotMonitoring() {
            return options -> options.addSpotEvents("missing.spot", Duration.ofMillis(100));
        }
    }
}
