package systems.zlink.e2e.kotlin.resiliencelifecycle

import com.fasterxml.jackson.databind.ObjectMapper
import java.time.Duration
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.locations.ZLinkLocationStore
import systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore
import systems.zlink.framework.spring.internal.runtime.ZLinkFrameworkLifecycle
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer

@EnableZLinkFramework
@SpringBootApplication(proxyBeanMethods = false)
open class ConsumerApplication {
    @Bean
    open fun objectMapper(): ObjectMapper = ObjectMapper()

    @Bean
    open fun consumerFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            val logDir = Env.get("ZLINK_KOTLIN_E2E_LOG_DIR", "logs")
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("$logDir/consumer-flow.log")
                .traceLabel("kotlin-rl-consumer")
            options.configureLocations().setOwnerLeaseTtl(Duration.ofSeconds(3))
            options.configureLocations().setHeartbeatInterval(Duration.ofMillis(500))
            options.configureLocations().setPollingInterval(Duration.ofMillis(200))
            options.addClientServerChannel(Contracts.CHANNEL).enableClient()
        }

    @Bean
    open fun locationStore(): ZLinkRedisLocationStore =
        ZLinkRedisLocationStore(
            ZLinkRedisLocationOptions()
                .setConnectionString(Env.get("ZLINK_KOTLIN_E2E_REDIS_LOCATION_ENDPOINT"))
                .setKeyPrefix(Env.get("ZLINK_KOTLIN_E2E_LOCATION_KEY_PREFIX")),
        )

    @Bean
    open fun consumerHttpServer(
        client: ZLinkClient,
        lifecycle: ZLinkFrameworkLifecycle,
        locations: ZLinkLocationStore,
        json: ObjectMapper,
    ): ConsumerHttpServer =
        ConsumerHttpServer(
            client,
            lifecycle,
            locations,
            json,
            Env.get("ZLINK_KOTLIN_E2E_CONSUMER_HTTP_ENDPOINT"),
        )

    companion object {
        @JvmStatic
        fun run(vararg args: String): AutoCloseable {
            val builder = SpringApplicationBuilder(ConsumerApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
