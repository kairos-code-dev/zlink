package systems.zlink.e2e.runtimemonitoring.trigger.validation;

import org.springframework.boot.SpringBootConfiguration;
import org.springframework.boot.autoconfigure.EnableAutoConfiguration;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkMonitoringOptionsCustomizer;

@EnableZLinkFramework
@SpringBootConfiguration(proxyBeanMethods = false)
@EnableAutoConfiguration
public class MissingSocketSourceConfig {
    @Bean
    ZLinkMonitoringOptionsCustomizer missingSocketMonitoring() {
        return options -> options.addSocketEvents("missing.socket");
    }
}
