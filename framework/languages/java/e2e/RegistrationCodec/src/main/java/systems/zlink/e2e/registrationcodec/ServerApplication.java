package systems.zlink.e2e.registrationcodec;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.protobuf.StringValue;
import java.util.Set;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.beans.factory.config.ConfigurableBeanFactory;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Scope;
import systems.zlink.e2e.registrationcodec.handlers.DiLifecycleRequestHandler;
import systems.zlink.e2e.registrationcodec.handlers.AttrEchoHandler;
import systems.zlink.e2e.registrationcodec.handlers.AutoRequestHandler;
import systems.zlink.e2e.registrationcodec.handlers.AutoSendHandler;
import systems.zlink.e2e.registrationcodec.handlers.JsonRequestHandler;
import systems.zlink.e2e.registrationcodec.handlers.JsonSendHandler;
import systems.zlink.e2e.registrationcodec.handlers.ManualRequestHandler;
import systems.zlink.e2e.registrationcodec.handlers.ManualSendHandler;
import systems.zlink.e2e.registrationcodec.handlers.MsgpackRequestHandler;
import systems.zlink.e2e.registrationcodec.handlers.MsgpackSendHandler;
import systems.zlink.e2e.registrationcodec.handlers.ProtobufRequestHandler;
import systems.zlink.e2e.registrationcodec.handlers.ProtobufSendHandler;
import systems.zlink.framework.codecs.msgpack.ZLinkMessagePackCodec;
import systems.zlink.framework.codecs.protobuf.ZLinkProtobufCodec;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.registrationcodec.handlers")
public final class ServerApplication {
    private ServerApplication() {
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(ServerApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    ScenarioState scenarioState() {
        return new ScenarioState();
    }

    @Bean
    ObjectMapper objectMapper() {
        return new ObjectMapper();
    }

    @Bean
    EvidenceHttpServer evidenceHttpServer(ScenarioState state, ObjectMapper json) {
        return new EvidenceHttpServer(state, json, Env.get("ZLINK_JAVA_E2E_HTTP_ENDPOINT"));
    }

    @Bean
    ZLinkFrameworkConfigurer serverFramework() {
        return options -> {
            String logDir = Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs");
            options.codecs().addJson();
            if (!"json-only".equals(Env.get("ZLINK_JAVA_E2E_CODEC_MODE", ""))) {
                options.codecs().use(ZLinkProtobufCodec.defaultCodec());
                options.codecs().use(ZLinkMessagePackCodec.forPayloadTypes(ServerApplication::isPackedType));
            }
            options.useFilter(FirstOrderFilter.class);
            options.useFilter(SecondOrderFilter.class);
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(logDir + "/server-flow.log")
                .traceLabel("java-rc-server");
            options.addHandlersFromPackageOf(AutoRequestHandler.class);
            var channel = options.addClientServerChannel(Contracts.CHANNEL)
                .enableServer(Env.get("ZLINK_JAVA_E2E_SERVER_ENDPOINT"))
                .addHandlerGroup(Contracts.AUTO_GROUP)
                .addHandlerGroup(Contracts.ATTR_GROUP);
            channel.addRequestHandler(
                ManualRequestHandler.class,
                Contracts.EchoManualRequest.class,
                Contracts.EchoReply.class,
                "EchoManual");
            channel.addSendHandler(
                ManualSendHandler.class,
                Contracts.EchoManualCommand.class,
                "EchoManual");
            channel.addRequestHandler(
                DiLifecycleRequestHandler.class,
                Contracts.DiLifecycleRequest.class,
                Contracts.DiLifecycleReply.class,
                "DiLifecycle");
            channel.addRequestHandler(
                JsonRequestHandler.class,
                Contracts.JsonEchoRequest.class,
                Contracts.EchoReply.class,
                "JsonEcho");
            channel.addSendHandler(
                JsonSendHandler.class,
                Contracts.JsonEchoCommand.class,
                "JsonEcho");
            channel.addRequestHandler(
                ProtobufRequestHandler.class,
                StringValue.class,
                StringValue.class,
                "ProtobufEcho");
            channel.addSendHandler(
                ProtobufSendHandler.class,
                StringValue.class,
                "ProtobufEcho");
            channel.addRequestHandler(
                MsgpackRequestHandler.class,
                Contracts.PackedEchoRequest.class,
                Contracts.PackedEchoReply.class,
                "MsgpackEcho");
            channel.addSendHandler(
                MsgpackSendHandler.class,
                Contracts.PackedEchoCommand.class,
                "MsgpackEcho");
        };
    }

    private static boolean isPackedType(Class<?> type) {
        return Set.of(
            Contracts.PackedEchoRequest.class,
            Contracts.PackedEchoReply.class,
            Contracts.PackedEchoCommand.class).contains(type);
    }

    @Bean AutoRequestHandler autoRequestHandler(ScenarioState state) { return new AutoRequestHandler(state); }
    @Bean AutoSendHandler autoSendHandler(ScenarioState state) { return new AutoSendHandler(state); }
    @Bean AttrEchoHandler attrEchoHandler(ScenarioState state) { return new AttrEchoHandler(state); }
    @Bean ManualRequestHandler manualRequestHandler(ScenarioState state) { return new ManualRequestHandler(state); }
    @Bean ManualSendHandler manualSendHandler(ScenarioState state) { return new ManualSendHandler(state); }
    @Bean DiLifecycleRequestHandler diLifecycleRequestHandler(
        org.springframework.beans.factory.ObjectProvider<DiScopedDependency> scoped,
        DiSingletonDependency singleton,
        ScenarioState state) {
        return new DiLifecycleRequestHandler(scoped, singleton, state);
    }
    @Bean DiSingletonDependency diSingletonDependency() { return new DiSingletonDependency(); }
    @Bean
    @Scope(ConfigurableBeanFactory.SCOPE_PROTOTYPE)
    DiScopedDependency diScopedDependency(ScenarioState state) { return new DiScopedDependency(state); }
    @Bean JsonRequestHandler jsonRequestHandler(ScenarioState state) { return new JsonRequestHandler(state); }
    @Bean JsonSendHandler jsonSendHandler(ScenarioState state) { return new JsonSendHandler(state); }
    @Bean ProtobufRequestHandler protobufRequestHandler(ScenarioState state) { return new ProtobufRequestHandler(state); }
    @Bean ProtobufSendHandler protobufSendHandler(ScenarioState state) { return new ProtobufSendHandler(state); }
    @Bean MsgpackRequestHandler msgpackRequestHandler(ScenarioState state) { return new MsgpackRequestHandler(state); }
    @Bean MsgpackSendHandler msgpackSendHandler(ScenarioState state) { return new MsgpackSendHandler(state); }
}
