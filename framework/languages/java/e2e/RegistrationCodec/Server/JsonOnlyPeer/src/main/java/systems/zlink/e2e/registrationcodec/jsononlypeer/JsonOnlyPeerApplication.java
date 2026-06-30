package systems.zlink.e2e.registrationcodec.jsononlypeer;

import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.e2e.registrationcodec.jsononlypeer.Configuration.ServerOptions;
import systems.zlink.e2e.registrationcodec.jsononlypeer.Endpoints.OperationalEndpoints;
import systems.zlink.e2e.registrationcodec.jsononlypeer.Handlers.JsonRequestHandler;
import systems.zlink.e2e.registrationcodec.jsononlypeer.Handlers.MsgpackRequestHandler;
import systems.zlink.e2e.registrationcodec.jsononlypeer.Infrastructure.EvidenceStore;
import systems.zlink.e2e.registrationcodec.shared.Contracts;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.registrationcodec.jsononlypeer.Handlers")
public final class JsonOnlyPeerApplication {
    public AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(JsonOnlyPeerApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean ServerOptions serverOptions() { return ServerOptions.fromEnv(); }
    @Bean EvidenceStore evidenceStore() { return new EvidenceStore(); }
    @Bean ObjectMapper objectMapper() { return new ObjectMapper(); }
    @Bean OperationalEndpoints operationalEndpoints(EvidenceStore evidence, ObjectMapper json, ServerOptions options) {
        return new OperationalEndpoints(evidence, json, options.httpEndpoint());
    }

    @Bean
    ZLinkFrameworkConfigurer serverFramework(ServerOptions options) {
        return framework -> {
            framework.codecs().addJson();
            framework.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(options.logDir() + "/json-only-flow.log")
                .traceLabel("java-rc-json-only");
            var channel = framework.addClientServerChannel(Contracts.CHANNEL)
                .enableServer(options.serverEndpoint());
            channel.addRequestHandler(
                JsonRequestHandler.class,
                Contracts.JsonEchoRequest.class,
                Contracts.EchoReply.class,
                "JsonEcho");
            channel.addRequestHandler(
                MsgpackRequestHandler.class,
                Contracts.PackedEchoRequest.class,
                Contracts.PackedEchoReply.class,
                "MsgpackEcho");
        };
    }

    @Bean JsonRequestHandler jsonRequestHandler(EvidenceStore evidence) { return new JsonRequestHandler(evidence); }
    @Bean MsgpackRequestHandler msgpackRequestHandler(EvidenceStore evidence) { return new MsgpackRequestHandler(evidence); }
}
