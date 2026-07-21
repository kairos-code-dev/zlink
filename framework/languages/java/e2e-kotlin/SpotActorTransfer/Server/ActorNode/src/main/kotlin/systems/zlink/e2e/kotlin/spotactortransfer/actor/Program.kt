package systems.zlink.e2e.kotlin.spotactortransfer.actor

import com.fasterxml.jackson.databind.ObjectMapper
import java.time.Duration
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.e2e.spotactortransfer.shared.Contracts
import systems.zlink.e2e.spotactortransfer.shared.Env
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer

@EnableZLinkFramework
@SpringBootApplication(proxyBeanMethods = false)
class Application {
    @Bean
    fun objectMapper(): ObjectMapper = ObjectMapper()

    @Bean
    fun evidenceStore(): EvidenceStore {
        val nodeRid = Env.require("ZLINK_JAVA_E2E_NODE_RID")
        val logDir = Env.require("ZLINK_JAVA_E2E_LOG_DIR")
        return EvidenceStore(nodeRid, "$logDir/$nodeRid.evidence.log")
    }

    @Bean
    fun gateStore(): GateStore = GateStore()

    @Bean
    fun domainStateStore(): DomainStateStore =
        DomainStateStore(Env.require("ZLINK_JAVA_E2E_LOG_DIR") + "/domain-state")

    @Bean
    fun actorNodeHttpServer(
        json: ObjectMapper,
        evidence: EvidenceStore,
        gates: GateStore,
        spots: systems.zlink.framework.spots.ZLinkSpotManager,
        actors: systems.zlink.framework.actors.ZLinkActorManager,
        actorClient: systems.zlink.framework.actors.ZLinkActorClient,
    ): ActorNodeHttpServer = ActorNodeHttpServer(
        Env.require("ZLINK_JAVA_E2E_HTTP_ENDPOINT"),
        json,
        evidence,
        gates,
        spots,
        actors,
        actorClient,
    )

    @Bean
    fun framework(evidence: EvidenceStore): ZLinkFrameworkConfigurer = ZLinkFrameworkConfigurer { options ->
        val nodeRid = evidence.nodeRid
        val logDir = Env.require("ZLINK_JAVA_E2E_LOG_DIR")
        options.addHandlersFromPackageOf(Application::class.java)
        options.setActorTransferForwardWindow(Duration.ofSeconds(2))
        options.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
            .traceLogFile("$logDir/$nodeRid-flow.log")
            .traceLabel("kotlin-spot-transfer-$nodeRid")
        val node = options.addRouteMesh(Contracts.MESH)
        node.listen(Env.require("ZLINK_JAVA_E2E_MESH_ENDPOINT"))
            .setRoutingId(RoutingId.from(nodeRid))
        node.channelName(Contracts.MESH)
        Env.require("ZLINK_JAVA_E2E_MESH_PEERS").split(',').forEach { peer ->
            val fields = peer.split('=', limit = 2)
            if (fields.size == 2 && fields[0] != nodeRid) {
                node.peerConnections().connect(RoutingId.from(fields[0]), fields[1])
            }
        }
        node.configureEntrySpot().setRoutingId(RoutingId.from(Contracts.ENTRY_SPOT_RID))
        node.addEntrySpot(TransferEntrySpot::class.java)
        registerActor(node, Contracts.STATEFUL, true)
        registerActor(node, Contracts.EMPTY_STATE, true)
        registerActor(node, Contracts.NO_ADAPTER, false)
        registerActor(node, Contracts.FAIL_OUT, true)
        registerActor(node, Contracts.FAIL_LEAVE, true)
        registerActor(node, Contracts.FAIL_IN, true)
        node.addSpotFactory(TransferUserSpot::class.java)
        options.addStreamNode("spot-transfer-session-$nodeRid")
            .bind(Env.require("ZLINK_JAVA_E2E_STREAM_ENDPOINT"))
            .enableActorDispatch(Contracts.MESH)
            .registerSession(TransferSession::class.java)
    }

    private fun registerActor(
        node: systems.zlink.framework.configuration.ZLinkMeshNodeBuilder,
        actorType: String,
        adapter: Boolean,
    ) {
        node.addActorFactory(actorType, TransferActorFactory::class.java)
        if (adapter) node.addActorTransferAdapter(actorType, TransferActorAdapter::class.java)
    }

    @Bean
    fun locationStore(): ZLinkRedisLocationStore = ZLinkRedisLocationStore(
        ZLinkRedisLocationOptions()
            .setConnectionString(Env.require("ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT"))
            .setKeyPrefix(Env.require("ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX")),
    )
}

fun main(vararg args: String) {
    val builder = SpringApplicationBuilder(Application::class.java).web(WebApplicationType.NONE)
    builder.application().setKeepAlive(true)
    builder.run(*args)
}
