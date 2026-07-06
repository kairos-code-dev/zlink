package systems.zlink.samples.kotlin.deliverydispatch.server.dispatch

import com.fasterxml.jackson.databind.ObjectMapper
import com.fasterxml.jackson.module.kotlin.KotlinModule
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleLocationStore
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleTopology

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [DispatchServerApplication::class],
)
class DispatchServerApplication {
    @Bean
    fun dispatchFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(
                    System.getenv().getOrDefault("DELIVERYDISPATCH_LOG_DIR", "logs") +
                        "/flow-dispatch.log",
                )
                .traceLabel("dispatch")
            options.addClientServerChannel(SampleNames.CourierChannel)
                .enableClient()
            options.addClientServerChannel(SampleNames.TrackingChannel)
                .enableClient()
        }

    @Bean
    fun locationStore(): ZLinkRedisLocationStore = SampleLocationStore.create()

    @Bean
    fun dispatchWorkQueue(worker: DispatchWorker): DispatchWorkQueue = DispatchWorkQueue(worker)

    @Bean
    fun dispatchWorker(channels: ZLinkClient): DispatchWorker = DispatchWorker(channels)

    @Bean
    fun dispatchHttpServer(
        json: ObjectMapper,
        queue: DispatchWorkQueue,
    ): DispatchHttpServer = DispatchHttpServer(json, queue)

    @Bean
    fun objectMapper(): ObjectMapper =
        ObjectMapper().registerModule(KotlinModule.Builder().build())

    companion object {
        fun run(args: Array<String> = emptyArray()): AutoCloseable {
            val builder = SpringApplicationBuilder(DispatchServerApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
