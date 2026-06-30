package systems.zlink.e2e.kotlin.resiliencelifecycle

import com.fasterxml.jackson.databind.JsonNode
import com.fasterxml.jackson.databind.ObjectMapper
import java.io.IOException
import java.net.URI
import java.net.http.HttpClient
import java.net.http.HttpRequest
import java.net.http.HttpResponse
import java.nio.file.Files
import java.nio.file.Path
import java.time.Duration
import java.util.concurrent.TimeUnit
import systems.zlink.contracts.service.registry.ServiceRole
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.registry.ZLinkRegistryQueryClient

class ClientScenarioContext(
    val client: ZLinkClient,
    val registry: ZLinkRegistryQueryClient?,
    val json: ObjectMapper,
    val options: ClientOptions,
) {
    private val http: HttpClient = HttpClient.newHttpClient()

    fun collectProviders(prefix: String, attempts: Int, expectedCount: Int): MutableSet<String> {
        val providers = linkedSetOf<String>()
        var index = 0
        while (index < attempts && providers.size < expectedCount) {
            val reply = client.requestToChannel(
                Contracts.CHANNEL,
                Contracts.WorkReq("$prefix-$index"),
            )
                .timeout(Duration.ofSeconds(3))
                .await(Contracts.WorkRes::class.java)
            ensure(reply.value() == "work:$prefix-$index", "reply payload mismatch for $prefix-$index")
            providers.add(reply.providerRid())
            index++
        }
        return providers
    }

    fun collectStableProvidersWithout(prefix: String, forbidden: String, required: String): Set<String> {
        repeat(30) { window ->
            val providers = collectProviders("$prefix-window-$window", 5, 1)
            if (!providers.contains(forbidden) && providers.contains(required)) {
                return providers
            }
            sleep(300)
        }
        throw IllegalStateException("$prefix did not converge away from $forbidden to $required")
    }

    fun waitForTopology(expectedRouters: Int) {
        val queryClient = registry ?: return
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(10)
        while (System.nanoTime() < deadline) {
            try {
                val count = queryClient.topology().toCompletableFuture()
                    .get(3, TimeUnit.SECONDS)
                    .asSequence()
                    .filter { it.channelName() == Contracts.CHANNEL }
                    .filter { it.serviceRole() == ServiceRole.ROUTER }
                    .count()
                if (count >= expectedRouters) {
                    return
                }
            } catch (_: Exception) {
            }
            sleep(200)
        }
        throw IllegalStateException(
            "registry topology did not report $expectedRouters routers for ${Contracts.CHANNEL}",
        )
    }

    fun waitForTopologyEndpoint(routingId: String, endpoint: String) {
        val queryClient = registry ?: throw IllegalStateException("registry query client is not configured")
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(15)
        while (System.nanoTime() < deadline) {
            try {
                val found = queryClient.topology().toCompletableFuture()
                    .get(3, TimeUnit.SECONDS)
                    .any { entry ->
                        entry.channelName() == Contracts.CHANNEL &&
                            entry.serviceRole() == ServiceRole.ROUTER &&
                            entry.routingId().toString() == routingId &&
                            entry.endpoint() == endpoint
                    }
                if (found) {
                    return
                }
            } catch (_: Exception) {
            }
            sleep(200)
        }
        throw IllegalStateException("registry topology did not report $routingId at $endpoint")
    }

    fun waitForWeight(baseUrl: String, expected: Int) {
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5)
        while (System.nanoTime() < deadline) {
            try {
                val node = json.readTree(get("$baseUrl/admin/weight"))
                if (node.path("weight").asInt(-1) == expected) {
                    return
                }
            } catch (_: Exception) {
            }
            sleep(100)
        }
        throw IllegalStateException("weight did not become $expected for $baseUrl")
    }

    fun waitForEvidence(baseUrl: String, marker: String) {
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(10)
        while (System.nanoTime() < deadline) {
            try {
                val entries = json.readTree(get("$baseUrl/evidence")).path("entries")
                if (entries.isArray) {
                    for (entry in entries) {
                        if (entry.path("marker").asText() == marker) {
                            return
                        }
                    }
                }
            } catch (_: Exception) {
            }
            sleep(100)
        }
        throw IllegalStateException("marker $marker was not observed at $baseUrl")
    }

    fun waitForEvidenceAny(marker: String, vararg baseUrls: String) {
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(10)
        while (System.nanoTime() < deadline) {
            for (baseUrl in baseUrls) {
                try {
                    val entries = json.readTree(get("$baseUrl/evidence")).path("entries")
                    if (entries.isArray) {
                        for (entry in entries) {
                            if (entry.path("marker").asText() == marker) {
                                return
                            }
                        }
                    }
                } catch (_: Exception) {
                }
            }
            sleep(100)
        }
        throw IllegalStateException("marker $marker was not observed at any provider")
    }

    fun waitForDispatchErrorAny(packetName: String, vararg baseUrls: String) {
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(30)
        while (System.nanoTime() < deadline) {
            for (baseUrl in baseUrls) {
                try {
                    val entries: JsonNode = json.readTree(get("$baseUrl/evidence")).path("entries")
                    if (entries.isArray) {
                        for (entry in entries) {
                            val marker = entry.path("marker").asText()
                            val value = entry.path("value").asText()
                            if (
                                marker == "DispatchError" &&
                                value.contains("HANDLER_MISSING") &&
                                value.contains("REPLY_ERROR") &&
                                value.contains(packetName)
                            ) {
                                return
                            }
                        }
                    }
                } catch (_: Exception) {
                }
            }
            sleep(100)
        }
        throw IllegalStateException("dispatch error marker for $packetName was not observed")
    }

    fun expectSingleProviderDownFailure(scenario: String, value: String) {
        try {
            client.requestToChannel(Contracts.CHANNEL, Contracts.WorkReq(value))
                .timeout(Duration.ofMillis(700))
                .await(Contracts.WorkRes::class.java)
            throw IllegalStateException("$scenario down-window request unexpectedly completed")
        } catch (_: RuntimeException) {
            // The scenario only requires a public failure while the sole admissible provider is down.
        }
    }

    fun adminA(): String =
        options.httpAEndpoint ?: throw IllegalStateException("ZLINK_KOTLIN_E2E_HTTP_A_ENDPOINT is required")

    fun adminB(): String =
        options.httpBEndpoint ?: throw IllegalStateException("ZLINK_KOTLIN_E2E_HTTP_B_ENDPOINT is required")

    fun signal(name: String) {
        val dir = controlDir()
        try {
            Files.createDirectories(dir)
            Files.writeString(dir.resolve(name), "ok\n")
        } catch (error: IOException) {
            throw IllegalStateException("failed to write control signal $name", error)
        }
    }

    fun waitForSignal(name: String) {
        val file = controlDir().resolve(name)
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(30)
        while (System.nanoTime() < deadline) {
            if (Files.exists(file)) {
                return
            }
            sleep(100)
        }
        throw IllegalStateException("control signal was not observed: $name")
    }

    fun hasSignal(name: String): Boolean = Files.exists(controlDir().resolve(name))

    private fun controlDir(): Path {
        val value = options.controlDir ?: throw IllegalStateException("ZLINK_KOTLIN_E2E_CONTROL_DIR is required")
        if (value.isBlank()) {
            throw IllegalStateException("ZLINK_KOTLIN_E2E_CONTROL_DIR is required")
        }
        return Path.of(value)
    }

    fun get(url: String): String {
        try {
            val request = HttpRequest.newBuilder(URI.create(url))
                .timeout(Duration.ofSeconds(5))
                .GET()
                .build()
            val response = http.send(request, HttpResponse.BodyHandlers.ofString())
            ensure(response.statusCode() in 200..299, "GET $url returned ${response.statusCode()}")
            return response.body()
        } catch (error: IOException) {
            throw IllegalStateException("GET failed: $url", error)
        } catch (error: InterruptedException) {
            Thread.currentThread().interrupt()
            throw IllegalStateException("GET interrupted: $url", error)
        }
    }

    fun post(url: String) {
        try {
            val request = HttpRequest.newBuilder(URI.create(url))
                .timeout(Duration.ofSeconds(5))
                .POST(HttpRequest.BodyPublishers.noBody())
                .build()
            val response = http.send(request, HttpResponse.BodyHandlers.ofString())
            ensure(response.statusCode() in 200..299, "POST $url returned ${response.statusCode()}")
        } catch (error: IOException) {
            throw IllegalStateException("POST failed: $url", error)
        } catch (error: InterruptedException) {
            Thread.currentThread().interrupt()
            throw IllegalStateException("POST interrupted: $url", error)
        }
    }
}

fun sleep(millis: Long) {
    try {
        Thread.sleep(millis)
    } catch (error: InterruptedException) {
        Thread.currentThread().interrupt()
        throw IllegalStateException("interrupted", error)
    }
}

fun ensure(condition: Boolean, message: String) {
    if (!condition) {
        throw IllegalStateException(message)
    }
}
