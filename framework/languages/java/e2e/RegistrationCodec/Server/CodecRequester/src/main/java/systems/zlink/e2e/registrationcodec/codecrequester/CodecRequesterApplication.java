package systems.zlink.e2e.registrationcodec.codecrequester;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.net.http.HttpClient;
import java.util.Set;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.ConfigurableApplicationContext;
import org.springframework.context.annotation.Bean;
import systems.zlink.e2e.registrationcodec.client.Scenarios.CodecMismatchScenario;
import systems.zlink.e2e.registrationcodec.client.Support.ClientOptions;
import systems.zlink.e2e.registrationcodec.client.Support.Evidence;
import systems.zlink.e2e.registrationcodec.client.Support.ScenarioContext;
import systems.zlink.e2e.registrationcodec.codecrequester.Configuration.CodecRequesterOptions;
import systems.zlink.e2e.registrationcodec.shared.Contracts;
import systems.zlink.framework.codecs.msgpack.ZLinkMessagePackCodec;
import systems.zlink.framework.codecs.protobuf.ZLinkProtobufCodec;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;

@EnableZLinkFramework
@SpringBootApplication(proxyBeanMethods = false)
public final class CodecRequesterApplication {
    public void run(String... args) {
        ConfigurableApplicationContext context =
            new SpringApplicationBuilder(CodecRequesterApplication.class)
                .web(WebApplicationType.NONE)
                .run(args);
        try {
            CodecMismatchScenario.run(context.getBean(ScenarioContext.class));
            System.out.println("registration-codec e2e result=passed");
        } finally {
            context.close();
        }
    }

    @Bean CodecRequesterOptions codecRequesterOptions() { return CodecRequesterOptions.fromEnv(); }

    @Bean
    ClientOptions clientOptions(CodecRequesterOptions options) {
        return new ClientOptions(options.serverEndpoint(), options.httpEndpoint(), options.logDir());
    }

    @Bean ObjectMapper objectMapper() { return new ObjectMapper(); }
    @Bean Evidence evidence(ClientOptions options, ObjectMapper json) {
        return new Evidence(options, json, HttpClient.newHttpClient());
    }
    @Bean ScenarioContext scenarioContext(systems.zlink.framework.channels.ZLinkClient client, Evidence evidence) {
        return new ScenarioContext(client, evidence);
    }

    @Bean
    ZLinkFrameworkConfigurer requesterFramework(CodecRequesterOptions options) {
        return framework -> {
            framework.codecs().addJson();
            framework.codecs().use(ZLinkProtobufCodec.defaultCodec());
            framework.codecs().use(ZLinkMessagePackCodec.forPayloadTypes(
                CodecRequesterApplication::isPackedType));
            framework.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(options.logDir() + "/codec-requester-flow.log")
                .traceLabel("java-rc-codec-requester");
            framework.addClientServerChannel(Contracts.CHANNEL)
                .enableClient(options.serverEndpoint());
        };
    }

    private static boolean isPackedType(Class<?> type) {
        return Set.of(
            Contracts.PackedEchoRequest.class,
            Contracts.PackedEchoReply.class,
            Contracts.PackedEchoCommand.class).contains(type);
    }
}
