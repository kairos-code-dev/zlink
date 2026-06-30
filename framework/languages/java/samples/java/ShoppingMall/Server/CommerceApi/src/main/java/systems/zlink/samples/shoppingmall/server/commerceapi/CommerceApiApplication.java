package systems.zlink.samples.shoppingmall.server.commerceapi;

import org.springframework.boot.ApplicationArguments;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.ConfigurableApplicationContext;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.samples.shoppingmall.server.configuration.CommerceStore;
import systems.zlink.samples.shoppingmall.server.configuration.SampleNames;
import systems.zlink.samples.shoppingmall.server.configuration.SampleTopology;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = CommerceApiApplication.class)
public final class CommerceApiApplication {
    private CommerceApiApplication() {
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(CommerceApiApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        ConfigurableApplicationContext context = builder.run(args);
        context.getBean(CommerceStore.class).seedDefaults();
        return context::close;
    }

    @Bean
    CommerceApiInstanceOptions instanceOptions(ApplicationArguments arguments) {
        return CommerceApiInstanceOptions.fromArgs(arguments.getSourceArgs());
    }

    @Bean
    CommerceStore commerceStore() {
        return new CommerceStore();
    }

    @Bean
    ZLinkFrameworkConfigurer commerceApiFramework(CommerceApiInstanceOptions options) {
        return configurer -> {
            configurer.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint);
            configurer.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(System.getenv().getOrDefault("SHOPPINGMALL_LOG_DIR", "logs") + "/flow-" + options.instanceId() + ".log")
                .traceLabel(options.instanceId());
            configurer.addHandlersFromPackageOf(CommerceApiApplication.class);

            // public client-facing channel hosted on this instance's endpoint
            configurer.addClientServerChannel(SampleNames.commerceApiChannel(options.instanceId()))
                .enableServer(SampleTopology.commerceApiEndpoint(options.instanceId()))
                .addHandlerGroup("commerce");

            // peer forwarding: client to the other CommerceApi instance
            String peer = CommerceApiInstanceOptions.peerInstance(options.instanceId());
            configurer.addClientServerChannel(SampleNames.commerceApiChannel(peer))
                .enableClient();

            // workflow routing: client to both OrderWorkflow instances
            configurer.addClientServerChannel(
                    SampleNames.workflowChannel(SampleNames.WorkflowInstanceA))
                .enableClient();
            configurer.addClientServerChannel(
                    SampleNames.workflowChannel(SampleNames.WorkflowInstanceB))
                .enableClient();
        };
    }
}
