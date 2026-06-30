package systems.zlink.samples.shoppingmall.client;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.samples.shoppingmall.client.configuration.SampleNames;
import systems.zlink.samples.shoppingmall.client.configuration.SampleTopology;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = ClientApplication.class)
public final class ClientApplication {
    private ClientApplication() {
    }

    public static void run(String... args) throws Exception {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(ClientApplication.class)
            .web(WebApplicationType.NONE);
        try (var context = builder.run(args)) {
            ZLinkClient channels = context.getBean(ZLinkClient.class);
            new ShoppingMallClientScenario(channels).run();
        }
        System.out.println("shoppingmall=completed");
    }

    @Bean
    ZLinkFrameworkConfigurer clientFramework() {
        return options -> {
            options.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint);
            options.addClientServerChannel(SampleNames.commerceApiChannel(SampleNames.ApiInstanceA))
                .enableClient();
            options.addClientServerChannel(SampleNames.commerceApiChannel(SampleNames.ApiInstanceB))
                .enableClient();
        };
    }
}
