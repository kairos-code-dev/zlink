package systems.zlink.e2e.runtimemonitoring.trigger.validation;

import java.time.Duration;
import org.springframework.boot.SpringBootConfiguration;
import org.springframework.boot.autoconfigure.EnableAutoConfiguration;
import org.springframework.context.annotation.Bean;
import systems.zlink.e2e.runtimemonitoring.shared.Contracts;
import systems.zlink.framework.spring.ZLinkMonitoringOptionsCustomizer;

@SpringBootConfiguration(proxyBeanMethods = false)
@EnableAutoConfiguration
public class BadIntervalConfig {
    @Bean
    ZLinkMonitoringOptionsCustomizer badIntervalMonitoring() {
        return options -> options.addRegistryEvents(Contracts.REGISTRY_SOURCE, Duration.ZERO);
    }
}
