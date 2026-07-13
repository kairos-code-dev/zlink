package systems.zlink.e2e.registrationcodec.main;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.protobuf.StringValue;
import java.util.Set;
import org.springframework.beans.factory.config.ConfigurableBeanFactory;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Scope;
import systems.zlink.e2e.registrationcodec.main.Configuration.ServerOptions;
import systems.zlink.e2e.registrationcodec.main.Endpoints.OperationalEndpoints;
import systems.zlink.e2e.registrationcodec.main.Handlers.AttrEchoHandler;
import systems.zlink.e2e.registrationcodec.main.Handlers.AutoRequestHandler;
import systems.zlink.e2e.registrationcodec.main.Handlers.AutoSendHandler;
import systems.zlink.e2e.registrationcodec.main.Handlers.DiLifecycleReqHandler;
import systems.zlink.e2e.registrationcodec.main.Handlers.FirstOrderFilter;
import systems.zlink.e2e.registrationcodec.main.Handlers.JsonRequestHandler;
import systems.zlink.e2e.registrationcodec.main.Handlers.JsonSendHandler;
import systems.zlink.e2e.registrationcodec.main.Handlers.ManualRequestHandler;
import systems.zlink.e2e.registrationcodec.main.Handlers.ManualSendHandler;
import systems.zlink.e2e.registrationcodec.main.Handlers.MsgpackRequestHandler;
import systems.zlink.e2e.registrationcodec.main.Handlers.MsgpackSendHandler;
import systems.zlink.e2e.registrationcodec.main.Handlers.ProtobufRequestHandler;
import systems.zlink.e2e.registrationcodec.main.Handlers.ProtobufSendHandler;
import systems.zlink.e2e.registrationcodec.main.Handlers.SecondOrderFilter;
import systems.zlink.e2e.registrationcodec.main.Infrastructure.DiScopedDependency;
import systems.zlink.e2e.registrationcodec.main.Infrastructure.DiSingletonDependency;
import systems.zlink.e2e.registrationcodec.main.Infrastructure.EvidenceStore;
import systems.zlink.e2e.registrationcodec.shared.Contracts;
import systems.zlink.framework.codecs.msgpack.ZLinkMessagePackCodec;
import systems.zlink.framework.codecs.protobuf.ZLinkProtobufCodec;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.registrationcodec.main.Handlers")
public final class RegistrationCodecServerApplication {
    public AutoCloseable run(String... args) {
        SpringApplicationBuilder builder =
            new SpringApplicationBuilder(RegistrationCodecServerApplication.class)
                .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    ServerOptions serverOptions() {
        return ServerOptions.fromEnv();
    }

    @Bean
    EvidenceStore evidenceStore() {
        return new EvidenceStore();
    }

    @Bean
    ObjectMapper objectMapper() {
        return new ObjectMapper();
    }

    @Bean
    OperationalEndpoints operationalEndpoints(
        EvidenceStore evidence,
        ObjectMapper json,
        systems.zlink.framework.channels.ZLinkClient client,
        ServerOptions options) {
        return new OperationalEndpoints(evidence, json, client, options.httpEndpoint());
    }

    @Bean
    ZLinkFrameworkConfigurer serverFramework(ServerOptions options) {
        return framework -> {
            framework.codecs().use(ZLinkProtobufCodec.defaultCodec());
            framework.codecs().use(ZLinkMessagePackCodec.forPayloadTypes(
                RegistrationCodecServerApplication::isPackedType));
            framework.useFilter(FirstOrderFilter.class);
            framework.useFilter(SecondOrderFilter.class);
            framework.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(options.logDir() + "/server-flow.log")
                .traceLabel("java-rc-server");
            framework.addHandlersFromPackageOf(AutoRequestHandler.class);
            var channel = framework.addClientServerChannel(Contracts.CHANNEL)
                .enableServer(options.serverEndpoint())
                .enableClient(options.serverEndpoint())
                .addHandlerGroup(Contracts.AUTO_GROUP)
                .addHandlerGroup(Contracts.ATTR_GROUP);
            channel.addRequestHandler(
                ManualRequestHandler.class,
                Contracts.EchoManualReq.class,
                Contracts.EchoRes.class,
                "EchoManual");
            channel.addSendHandler(
                ManualSendHandler.class,
                Contracts.EchoManualMsg.class,
                "EchoManual");
            channel.addRequestHandler(
                DiLifecycleReqHandler.class,
                Contracts.DiLifecycleReq.class,
                Contracts.DiLifecycleRes.class,
                "DiLifecycle");
            channel.addRequestHandler(
                JsonRequestHandler.class,
                Contracts.JsonEchoReq.class,
                Contracts.EchoRes.class,
                "JsonEcho");
            channel.addSendHandler(
                JsonSendHandler.class,
                Contracts.JsonEchoMsg.class,
                "JsonEcho");
            channel.addRequestHandler(
                ProtobufRequestHandler.class,
                StringValue.class,
                StringValue.class);
            channel.addSendHandler(
                ProtobufSendHandler.class,
                StringValue.class);
            channel.addRequestHandler(
                MsgpackRequestHandler.class,
                Contracts.PackedEchoReq.class,
                Contracts.PackedEchoRes.class,
                "MsgpackEcho");
            channel.addSendHandler(
                MsgpackSendHandler.class,
                Contracts.PackedEchoMsg.class,
                "MsgpackEcho");
        };
    }

    private static boolean isPackedType(Class<?> type) {
        return Set.of(
            Contracts.PackedEchoReq.class,
            Contracts.PackedEchoRes.class,
            Contracts.PackedEchoMsg.class).contains(type);
    }

    @Bean AutoRequestHandler autoRequestHandler(EvidenceStore evidence) { return new AutoRequestHandler(evidence); }
    @Bean AutoSendHandler autoSendHandler(EvidenceStore evidence) { return new AutoSendHandler(evidence); }
    @Bean AttrEchoHandler attrEchoHandler(EvidenceStore evidence) { return new AttrEchoHandler(evidence); }
    @Bean ManualRequestHandler manualRequestHandler(EvidenceStore evidence) { return new ManualRequestHandler(evidence); }
    @Bean ManualSendHandler manualSendHandler(EvidenceStore evidence) { return new ManualSendHandler(evidence); }
    @Bean DiLifecycleReqHandler diLifecycleRequestHandler(
        org.springframework.beans.factory.ObjectProvider<DiScopedDependency> scoped,
        DiSingletonDependency singleton,
        EvidenceStore evidence) {
        return new DiLifecycleReqHandler(scoped, singleton, evidence);
    }
    @Bean DiSingletonDependency diSingletonDependency() { return new DiSingletonDependency(); }
    @Bean
    @Scope(ConfigurableBeanFactory.SCOPE_PROTOTYPE)
    DiScopedDependency diScopedDependency(EvidenceStore evidence) { return new DiScopedDependency(evidence); }
    @Bean JsonRequestHandler jsonRequestHandler(EvidenceStore evidence) { return new JsonRequestHandler(evidence); }
    @Bean JsonSendHandler jsonSendHandler(EvidenceStore evidence) { return new JsonSendHandler(evidence); }
    @Bean ProtobufRequestHandler protobufRequestHandler(EvidenceStore evidence) { return new ProtobufRequestHandler(evidence); }
    @Bean ProtobufSendHandler protobufSendHandler(EvidenceStore evidence) { return new ProtobufSendHandler(evidence); }
    @Bean MsgpackRequestHandler msgpackRequestHandler(EvidenceStore evidence) { return new MsgpackRequestHandler(evidence); }
    @Bean MsgpackSendHandler msgpackSendHandler(EvidenceStore evidence) { return new MsgpackSendHandler(evidence); }
}
