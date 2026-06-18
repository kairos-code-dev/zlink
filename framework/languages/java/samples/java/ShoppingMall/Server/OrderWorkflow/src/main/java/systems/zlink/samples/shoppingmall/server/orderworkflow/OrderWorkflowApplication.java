package systems.zlink.samples.shoppingmall.server.orderworkflow;

import org.springframework.boot.ApplicationArguments;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.samples.shoppingmall.server.configuration.CommerceStore;
import systems.zlink.samples.shoppingmall.server.configuration.SampleNames;
import systems.zlink.samples.shoppingmall.server.configuration.SampleTopology;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = OrderWorkflowApplication.class)
public final class OrderWorkflowApplication {
    private OrderWorkflowApplication() {
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(OrderWorkflowApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    OrderWorkflowInstanceOptions instanceOptions(ApplicationArguments arguments) {
        return OrderWorkflowInstanceOptions.fromArgs(arguments.getSourceArgs());
    }

    @Bean
    CommerceStore commerceStore() {
        return new CommerceStore();
    }

    @Bean
    ZLinkFrameworkConfigurer orderWorkflowFramework(OrderWorkflowInstanceOptions options) {
        return configurer -> {
            configurer.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint);
            configurer.codecs().addJson();
            configurer.addHandlersFromPackageOf(OrderWorkflowApplication.class);
            configurer.addClientServerChannel(SampleNames.workflowChannel(options.instanceId()))
                .enableServer(SampleTopology.workflowEndpoint(options.instanceId()))
                .addHandlerGroup("workflow");
        };
    }
}
