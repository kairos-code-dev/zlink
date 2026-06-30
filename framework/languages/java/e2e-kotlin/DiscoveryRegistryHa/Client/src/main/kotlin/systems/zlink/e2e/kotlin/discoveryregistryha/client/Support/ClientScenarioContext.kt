package systems.zlink.e2e.kotlin.discoveryregistryha.client.Support

import com.fasterxml.jackson.databind.ObjectMapper
import java.net.URI
import java.net.http.HttpClient
import java.net.http.HttpRequest
import java.net.http.HttpResponse
import java.time.Duration
import java.util.concurrent.TimeUnit
import systems.zlink.e2e.kotlin.discoveryregistryha.ClientOptions
import systems.zlink.e2e.kotlin.discoveryregistryha.Contracts

class ClientScenarioContext(
    private val json: ObjectMapper,
    val options: ClientOptions,
) {
    private val http = HttpClient.newHttpClient()

    fun runChannelScenario(
        waitForMembers: Boolean = options.waitForMembers(),
        expectDeadProbeFailure: Boolean = false,
        expectTopologyMatch: Boolean = false,
    ) {
        if (waitForMembers) {
            waitForExpectedMembers()
        }
        val providers = requestUntilProviders(options.expectedRids())
        if (expectDeadProbeFailure) {
            expectHttpFailure(options.deadHttpEndpoint())
        }
        if (expectTopologyMatch) {
            expectRemoteTopologyMatchesProbe()
        }
        println("scenario ${options.scenario()} passed providers=$providers")
    }

    private fun waitForExpectedMembers() {
        val expected = options.expectedRids().toSet()
        for (probe in options.probeHttpEndpoints()) {
            val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(15)
            while (System.nanoTime() < deadline) {
                if (memberRids(probe).containsAll(expected)) {
                    break
                }
                sleep(250)
            }
            val observed = memberRids(probe)
            ScenarioAssert.that(
                observed.containsAll(expected),
                "probe $probe missing expected members $expected: $observed",
            )
        }
    }

    private fun memberRids(probe: String): Set<String> =
        try {
            json.readTree(get("$probe/member-peers"))
                .mapNotNull { entry -> entry.path("routingId").asText("").takeIf(String::isNotBlank) }
                .toSet()
        } catch (_: Exception) {
            emptySet()
        }

    private fun expectRemoteTopologyMatchesProbe() {
        val probeRids = topologyRids(options.topologyHttpEndpoint())
        val remoteRids = topologyRids(options.remoteProbeHttpEndpoint())
        ScenarioAssert.that(
            remoteRids == probeRids,
            "remote topology did not match in-process probe: remote=$remoteRids probe=$probeRids",
        )
    }

    private fun topologyRids(probe: String?): Set<String> {
        ScenarioAssert.that(!probe.isNullOrBlank(), "topology probe endpoint is required")
        try {
            return json.readTree(get("$probe/topology-rids"))
                .mapNotNull { entry -> entry.asText("").takeIf(String::isNotBlank) }
                .toSet()
        } catch (error: Exception) {
            throw IllegalStateException("topology probe query failed: $probe", error)
        }
    }

    private fun requestUntilProviders(expected: List<String>): Set<String> {
        val providers = linkedSetOf<String>()
        for (index in 0 until 120) {
            if (providers.containsAll(expected)) {
                break
            }
            val reply = postWorkRequest("msg-$index")
            ScenarioAssert.that(reply.value == "work:msg-$index", "reply payload mismatch")
            providers.add(reply.providerRid)
        }
        ScenarioAssert.that(
            providers.containsAll(expected),
            "messaging did not reach expected providers $expected: $providers",
        )
        return providers
    }

    private fun postWorkRequest(value: String): Contracts.WorkReply {
        val endpoint = options.consumerHttpEndpoint()
        ScenarioAssert.that(!endpoint.isNullOrBlank(), "consumer HTTP endpoint is required")
        val body = json.writeValueAsString(Contracts.WorkRequest(value))
        val responseBody = post("$endpoint/profile/request", body)
        return json.readValue(responseBody, Contracts.WorkReply::class.java)
    }

    private fun expectHttpFailure(url: String?) {
        if (url.isNullOrBlank()) {
            return
        }
        try {
            get("$url/health")
            throw IllegalStateException("dead registry probe unexpectedly responded: $url")
        } catch (expected: IllegalStateException) {
            if (expected.message?.startsWith("dead registry probe") == true) {
                throw expected
            }
        }
    }

    private fun get(url: String): String =
        try {
            val response = http.send(
                HttpRequest.newBuilder(URI.create(url))
                    .timeout(Duration.ofSeconds(2))
                    .GET()
                    .build(),
                HttpResponse.BodyHandlers.ofString(),
            )
            ScenarioAssert.that(
                response.statusCode() in 200..299,
                "GET $url returned ${response.statusCode()}",
            )
            response.body()
        } catch (error: java.io.IOException) {
            throw IllegalStateException("GET failed: $url", error)
        } catch (error: InterruptedException) {
            Thread.currentThread().interrupt()
            throw IllegalStateException("GET interrupted: $url", error)
        }

    private fun post(url: String, body: String): String =
        try {
            val response = http.send(
                HttpRequest.newBuilder(URI.create(url))
                    .timeout(Duration.ofSeconds(12))
                    .header("Content-Type", "application/json")
                    .POST(HttpRequest.BodyPublishers.ofString(body))
                    .build(),
                HttpResponse.BodyHandlers.ofString(),
            )
            ScenarioAssert.that(
                response.statusCode() in 200..299,
                "POST $url returned ${response.statusCode()}: ${response.body()}",
            )
            response.body()
        } catch (error: java.io.IOException) {
            throw IllegalStateException("POST failed: $url", error)
        } catch (error: InterruptedException) {
            Thread.currentThread().interrupt()
            throw IllegalStateException("POST interrupted: $url", error)
        }

    private fun sleep(millis: Long) {
        try {
            Thread.sleep(millis)
        } catch (error: InterruptedException) {
            Thread.currentThread().interrupt()
            throw IllegalStateException("interrupted", error)
        }
    }
}
