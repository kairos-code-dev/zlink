package systems.zlink.e2e.runtimemonitoring.trigger.validation;

import java.time.Duration;
import org.springframework.boot.SpringBootConfiguration;
import org.springframework.boot.autoconfigure.EnableAutoConfiguration;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkMonitoringOptionsCustomizer;

@EnableZLinkFramework
@SpringBootConfiguration(proxyBeanMethods = false)
@EnableAutoConfiguration
public class MissingSpotSourceConfig {
    @Bean
    ZLinkMonitoringOptionsCustomizer missingSpotMonitoring() {
        return options -> options.addSpotEvents("missing.spot", Duration.ofMillis(100));
    }
}
