package systems.zlink.e2e.registrationcodec.client;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.net.http.HttpClient;
import java.util.Set;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.ConfigurableApplicationContext;
import org.springframework.context.annotation.Bean;
import systems.zlink.e2e.registrationcodec.client.Support.ClientOptions;
import systems.zlink.e2e.registrationcodec.client.Support.Evidence;
import systems.zlink.e2e.registrationcodec.client.Support.ScenarioContext;
import systems.zlink.e2e.registrationcodec.shared.Contracts;
import systems.zlink.framework.codecs.msgpack.ZLinkMessagePackCodec;
import systems.zlink.framework.codecs.protobuf.ZLinkProtobufCodec;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;

@EnableZLinkFramework
@SpringBootApplication(proxyBeanMethods = false)
public final class RegistrationCodecClientApplication {
    public void run(String... args) {
        ConfigurableApplicationContext context =
            new SpringApplicationBuilder(RegistrationCodecClientApplication.class)
                .web(WebApplicationType.NONE)
                .run(args);
        try {
            context.getBean(ScenarioRunner.class).run();
            System.out.println("registration-codec e2e result=passed");
        } finally {
            context.close();
        }
    }

    @Bean
    ClientOptions clientOptions() {
        return ClientOptions.fromEnv();
    }

    @Bean
    ObjectMapper objectMapper() {
        return new ObjectMapper();
    }

    @Bean
    Evidence evidence(ClientOptions options, ObjectMapper json) {
        return new Evidence(options, json, HttpClient.newHttpClient());
    }

    @Bean
    ScenarioContext scenarioContext(
        systems.zlink.framework.channels.ZLinkClient client,
        Evidence evidence) {
        return new ScenarioContext(client, evidence);
    }

    @Bean
    ScenarioRunner scenarioRunner(ScenarioContext context) {
        return new ScenarioRunner(context);
    }

    @Bean
    ZLinkFrameworkConfigurer clientFramework(ClientOptions options) {
        return framework -> {
            framework.codecs().use(ZLinkProtobufCodec.defaultCodec());
            framework.codecs().use(ZLinkMessagePackCodec.forPayloadTypes(
                RegistrationCodecClientApplication::isPackedType));
            framework.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(options.logDir() + "/client-flow.log")
                .traceLabel("java-rc-client");
            framework.addClientServerChannel(Contracts.CHANNEL)
                .enableClient(options.serverEndpoint());
        };
    }

    private static boolean isPackedType(Class<?> type) {
        return Set.of(
            Contracts.PackedEchoReq.class,
            Contracts.PackedEchoRes.class,
            Contracts.PackedEchoMsg.class).contains(type);
    }
}
