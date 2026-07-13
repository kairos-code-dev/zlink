package systems.zlink.framework.kotlin

import java.lang.reflect.Modifier
import java.security.MessageDigest
import kotlin.coroutines.Continuation
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test

class KotlinPublicSurfaceContractTest {
    private val facadeClasses = listOf(
        "ZLinkConnectorExtensionsKt",
        "ZLinkCoroutineHandlerOptionsKt",
        "ZLinkCoroutineTurnAwaitKt",
        "ZLinkDispatchOptionsExtensionsKt",
        "ZLinkFrameworkExtensionsKt",
        "ZLinkLocationExtensionsKt",
        "ZLinkMessageExtensionsKt",
        "ZLinkSpotHandlerRegistryExtensionsKt",
    ).map { Class.forName("systems.zlink.framework.kotlin.$it") }

    @Test
    fun `public coroutine surface exposes one await terminator and no yield alternative`() {
        val surfaceClasses = listOf(
            Class.forName("systems.zlink.framework.kotlin.ZLinkCoroutineTurnAwaitKt"),
            Class.forName("systems.zlink.framework.kotlin.ZLinkFrameworkExtensionsKt"),
            Class.forName("systems.zlink.framework.kotlin.ZLinkConnectorExtensionsKt"),
        )
        val publicMethods = surfaceClasses
            .flatMap { it.declaredMethods.asList() }
            .filter { Modifier.isPublic(it.modifiers) }

        assertTrue(publicMethods.any { method ->
            method.name == "await" && method.parameterTypes.lastOrNull() == Continuation::class.java
        })
        assertFalse(publicMethods.any { it.name.contains("yield", ignoreCase = true) })
        assertFalse(publicMethods.any { method ->
            method.parameterTypes.any { it.simpleName.contains("CancellationToken") }
        })
    }

    @Test
    fun `typed and raw connector requests have coroutine terminators without signature clashes`() {
        val methods = Class.forName("systems.zlink.framework.kotlin.ZLinkConnectorExtensionsKt")
            .declaredMethods
            .filter { Modifier.isPublic(it.modifiers) }

        val signatures = methods.map { method ->
            method.name to method.parameterTypes.toList()
        }
        assertEqualsDistinct(signatures)
        assertTrue(methods.any { it.name == "awaitReply" })
        assertTrue(methods.count { it.name == "await" } >= 3)
    }

    @Test
    fun `documented Kotlin types and function names remain public`() {
        val expectedTypes = setOf(
            "ZLinkSuspendingRequestHandler",
            "ZLinkSuspendingSendHandler",
            "ZLinkSuspendingPublishHandler",
            "ZLinkSuspendingRouteRequestHandler",
            "ZLinkSuspendingRouteSendHandler",
            "ZLinkSuspendingSpotPacketHandler",
            "ZLinkSuspendingSpotRequestHandler",
            "ZLinkSuspendingSpotSubscriptionHandler",
            "ZLinkSuspendingSpotTimerHandler",
            "ZLinkSuspendingEntrySpotActorSendHandler",
            "ZLinkSuspendingEntrySpotActorRequestHandler",
            "ZLinkSuspendingSpotActorSendHandler",
            "ZLinkSuspendingSpotActorRequestHandler",
            "ZLinkSuspendingTypedSessionPacketHandler",
            "ZLinkSuspendingActorFactory",
            "ZLinkSuspendingActorTransferAdapter",
            "ZLinkSuspendingSpot",
            "ZLinkSuspendingEntrySpot",
            "ZLinkSuspendingSession",
            "ZLinkCoroutineSuspendHandlerInvoker",
            "ZLinkKotlinLifecycleCall",
            "ZLinkKotlinSendCall",
            "ZLinkKotlinStreamConnector",
            "ZLinkStreamTypedWaitCall",
            "ZLinkSuspendingLocationStore",
        )
        expectedTypes.forEach { Class.forName("systems.zlink.framework.kotlin.$it") }

        val publicFunctionNames = facadeClasses
            .flatMap { it.declaredMethods.asList() }
            .filter { Modifier.isPublic(it.modifiers) }
            .map { it.name }
            .toSet()
        val expectedFunctionNames = setOf(
            "actorRef", "addHandler", "actors", "asFlow", "await", "awaitReply",
            "bindOrGetActor", "changes",
            "configureStreamCompression", "configureDispatch", "create", "decode",
            "ensureActor", "errors", "findActor", "getOrCreate", "isPeerReady", "kotlin",
            "listActorLocations", "listLivePeers", "awaitOwnerLeases", "listPeerLocations",
            "listRouteLocations", "listServiceSummaries", "listSpotLocations", "listTopology",
            "locationPages", "messageOf", "messages", "onMessageFlow", "publishToTopic",
            "request", "removeActor", "removeAllByOwner", "removeOwnerLease", "removePeer",
            "removeRoute", "removeSpot", "renewOwnerLease", "requestToActorAwait",
            "resolveActor", "resolveActorSpotHandle", "resolveRoute", "resolveSpot",
            "resolveSpotHandle", "routes", "send", "snapshot", "spots", "status", "topology",
            "updateActor", "updatePeer", "updateRoute", "updateSpot", "useCoroutineHandlers",
            "waitFor", "withDefaultStreamCompression", "withLz4StreamCompression",
            "withStreamCompression", "withoutStreamCompression",
        )
        assertTrue(
            publicFunctionNames.containsAll(expectedFunctionNames),
            "missing Kotlin public functions: ${expectedFunctionNames - publicFunctionNames}",
        )
    }

    @Test
    fun `documented top level extension overloads remain fixed`() {
        assertFacadeMethodCounts(
            "ZLinkConnectorExtensionsKt",
            mapOf(
                "kotlin" to 1, "withDefaultStreamCompression" to 1,
                "withLz4StreamCompression" to 1, "withStreamCompression" to 1,
                "withoutStreamCompression" to 1, "await" to 3,
                "awaitReply" to 2, "awaitTyped" to 1, "waitFor" to 1,
                "messages" to 1, "errors" to 1,
            ),
        )
        assertFacadeMethodCounts(
            "ZLinkCoroutineHandlerOptionsKt",
            mapOf("useCoroutineHandlers" to 2),
        )
        assertFacadeMethodCounts("ZLinkCoroutineTurnAwaitKt", mapOf("await" to 1))
        assertFacadeMethodCounts(
            "ZLinkDispatchOptionsExtensionsKt",
            mapOf("configureDispatch" to 1, "onMessageFlow" to 1),
        )
        assertFacadeMethodCounts(
            "ZLinkFrameworkExtensionsKt",
            mapOf(
                "awaitReply" to 4, "requestToActorAwait" to 2,
                "findActor" to 1, "ensureActor" to 2, "snapshot" to 1,
                "actorRef" to 1, "isPeerReady" to 1, "bindOrGetActor" to 1,
                "awaitJoinCallVoid" to 1, "awaitJoinCall" to 1,
                "awaitJoinCallReified" to 1, "send" to 3, "request" to 3,
                "publishToTopic" to 1, "create" to 3, "getOrCreate" to 2,
                "configureStreamCompression" to 1,
            ),
        )
        assertFacadeMethodCounts(
            "ZLinkLocationExtensionsKt",
            mapOf(
                "locationPages" to 1, "spots" to 1, "actors" to 1,
                "routes" to 1, "topology" to 1, "updatePeer" to 1,
                "removePeer" to 1, "listPeerLocations" to 2,
                "updateSpot" to 1, "removeSpot" to 1, "resolveSpot" to 1,
                "listSpotLocations" to 2, "updateActor" to 1,
                "removeActor" to 1, "resolveActor" to 1,
                "listActorLocations" to 2, "updateRoute" to 1,
                "removeRoute" to 1, "resolveRoute" to 1,
                "listRouteLocations" to 2, "renewOwnerLease" to 1,
                "removeOwnerLease" to 1, "removeAllByOwner" to 1,
                "awaitOwnerLeases" to 1, "listLivePeers" to 1,
                "resolveSpotHandle" to 1, "resolveActorSpotHandle" to 1,
                "status" to 1, "listTopology" to 1,
                "listServiceSummaries" to 1, "changes" to 1, "asFlow" to 1,
            ),
        )
        assertFacadeMethodCounts(
            "ZLinkMessageExtensionsKt",
            mapOf("messageOf" to 1, "decode" to 1),
        )
        assertFacadeMethodCounts(
            "ZLinkSpotHandlerRegistryExtensionsKt",
            mapOf("addHandler" to 1, "addTypedHandler" to 1),
        )
    }

    @Test
    fun `one way Kotlin calls do not expose coroutine completion`() {
        val methods = facadeClasses
            .flatMap { it.declaredMethods.asList() }
            .filter { Modifier.isPublic(it.modifiers) }
        assertFalse(methods.any { it.name == "awaitSend" || it.name == "sendToActorAwait" })
        assertFalse(methods.any { it.name.contains("yield", ignoreCase = true) })
        val publishMethods = methods.filter { it.name == "publishToTopic" }
        assertTrue(publishMethods.isNotEmpty())
        assertFalse(publishMethods.any { it.parameterTypes.lastOrNull() == Continuation::class.java })

        val typedWaitMethods = Class.forName(
            "systems.zlink.framework.kotlin.ZLinkStreamTypedWaitCall",
        ).declaredMethods.filter { Modifier.isPublic(it.modifiers) }
        assertFalse(typedWaitMethods.any { it.name == "submit" })
    }

    @Test
    fun `Kotlin connector wrappers expose exactly the documented method groups`() {
        assertPublicMethodCounts(
            "ZLinkKotlinStreamConnector",
            mapOf(
                "isConnected" to 1, "getState" to 1, "getOptions" to 1,
                "getPendingDispatchCount" to 1, "receivedCount" to 1,
                "observeInbound" to 1, "on" to 2, "onErrorReceived" to 1,
                "onDisconnected" to 1, "onConnectionStateChanged" to 1,
                "connect" to 1, "disconnect" to 1, "reconnect" to 1,
                "close" to 1, "dispatch" to 1, "send" to 2, "request" to 2,
                "waitFor" to 2, "messages" to 1, "errors" to 1,
            ),
        )
        assertPublicMethodCounts("ZLinkKotlinLifecycleCall", mapOf("await" to 1))
        assertPublicMethodCounts("ZLinkKotlinSendCall", mapOf("submit" to 1))
        assertPublicMethodCounts(
            "ZLinkStreamTypedWaitCall",
            mapOf("timeout" to 1, "where" to 1, "await" to 1),
        )
    }

    @Test
    fun `documented Kotlin APIs retain their exact JVM descriptors`() {
        val expectedHashes = mapOf(
            "ZLinkConnectorExtensionsKt" to "f07a413711383a5a33db844b62d4b08b39d834b4c29e13aa252af39606d6d19c",
            "ZLinkCoroutineHandlerOptionsKt" to "67fda6a26015bcd374098db883ec13f012b2536da914e6b3e8fb0f6aea9e86f4",
            "ZLinkCoroutineTurnAwaitKt" to "0e58ca9d82f2e14d26e4763e955296534d7ce8e863c8fdd5b5231148a5661d5f",
            "ZLinkDispatchOptionsExtensionsKt" to "eb26c767da350a140c90c974e5485a3ba7af9265bb0f6badec8a381efb591443",
            "ZLinkFrameworkExtensionsKt" to "8ff8d8d4fed0323aa8f769e1caa1d7ecdbf3825d5e7a5d977af28652de1039de",
            "ZLinkLocationExtensionsKt" to "b2995174d76b4185bc8e7deb741fbc3e27ae9b94833ba6cb6f8b6325b2e31af6",
            "ZLinkMessageExtensionsKt" to "836b0c8038be8ee1beae9f8cf1f59cbd7e0811e936d1a4d47e7625b37abdaa9e",
            "ZLinkSpotHandlerRegistryExtensionsKt" to "0cc8a319eb99070b97332cab96c480fc74c14b9b160b022fa8d60ab4de814196",
            "ZLinkKotlinStreamConnector" to "c8e8a1bd37072daba92df701ed3cfc41b9649884244cb957f1d536643fbba82a",
            "ZLinkKotlinLifecycleCall" to "bef9eb581a23386b7802f54c64e3fec57c9920a17745c00c59195f7e67949aa5",
            "ZLinkKotlinSendCall" to "175454d3701a871365fbb774ca9e05c06e11ccdeb78fe013d874c704e2da56f0",
            "ZLinkStreamTypedWaitCall" to "6385a73bc528712e6d0f31512ba8f29c1951b2c347c48b6001f03c34e80d84f4",
        )
        expectedHashes.forEach { (typeName, expectedHash) ->
            val signatures = publicJvmSignatures(typeName)
            val actualHash = MessageDigest.getInstance("SHA-256")
                .digest((signatures.joinToString("\n") + "\n").toByteArray())
                .joinToString("") { byte -> "%02x".format(byte) }
            assertEquals(
                expectedHash,
                actualHash,
                "$typeName JVM descriptors changed: $signatures",
            )
        }
    }

    private fun publicJvmSignatures(typeName: String): List<String> =
        Class.forName("systems.zlink.framework.kotlin.$typeName")
            .declaredMethods
            .filter { method ->
                Modifier.isPublic(method.modifiers) &&
                    method.name != "awaitFrameworkStage" &&
                    !method.name.endsWith("\$default") &&
                    !method.name.startsWith("access\$") &&
                    !method.name.startsWith("getInner")
            }
            .map { method ->
                method.name + " " + method.parameterTypes.joinToString(
                    separator = "",
                    prefix = "(",
                    postfix = ")",
                ) { jvmDescriptor(it) } + jvmDescriptor(method.returnType)
            }
            .sorted()

    private fun jvmDescriptor(type: Class<*>): String = when {
        type.isArray -> type.name.replace('.', '/')
        !type.isPrimitive -> "L${type.name.replace('.', '/')};"
        type == Void.TYPE -> "V"
        type == Boolean::class.javaPrimitiveType -> "Z"
        type == Byte::class.javaPrimitiveType -> "B"
        type == Char::class.javaPrimitiveType -> "C"
        type == Short::class.javaPrimitiveType -> "S"
        type == Int::class.javaPrimitiveType -> "I"
        type == Long::class.javaPrimitiveType -> "J"
        type == Float::class.javaPrimitiveType -> "F"
        type == Double::class.javaPrimitiveType -> "D"
        else -> error("unsupported primitive JVM type: $type")
    }

    private fun assertPublicMethodCounts(typeName: String, expected: Map<String, Int>) {
        val actual = Class.forName("systems.zlink.framework.kotlin.$typeName")
            .declaredMethods
            .filter { Modifier.isPublic(it.modifiers) && !it.name.startsWith("getInner") }
            .groupingBy { it.name }
            .eachCount()
        assertEquals(expected, actual, "$typeName public method overloads changed")
    }

    private fun assertFacadeMethodCounts(typeName: String, expected: Map<String, Int>) {
        val ignoredNames = setOf("awaitFrameworkStage")
        val actual = Class.forName("systems.zlink.framework.kotlin.$typeName")
            .declaredMethods
            .filter { method ->
                Modifier.isPublic(method.modifiers) &&
                    method.name !in ignoredNames &&
                    !method.name.endsWith("\$default") &&
                    !method.name.startsWith("access\$")
            }
            .groupingBy { it.name }
            .eachCount()
        assertEquals(expected, actual, "$typeName public extension overloads changed")
    }

    private fun assertEqualsDistinct(signatures: List<Pair<String, List<Class<*>>>>) {
        assertTrue(signatures.size == signatures.distinct().size)
    }
}
