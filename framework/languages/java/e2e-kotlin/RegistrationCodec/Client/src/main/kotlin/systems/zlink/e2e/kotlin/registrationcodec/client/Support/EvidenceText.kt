package systems.zlink.e2e.kotlin.registrationcodec.client.support

import com.fasterxml.jackson.databind.ObjectMapper
import java.net.URI
import java.net.http.HttpClient
import java.net.http.HttpRequest
import java.net.http.HttpResponse
import java.time.Duration
import systems.zlink.e2e.kotlin.registrationcodec.EvidenceSnapshot

class EvidenceText(
    private val httpEndpoint: String,
    private val json: ObjectMapper,
    private val assert: ScenarioAssert,
) {
    private val http = HttpClient.newHttpClient()

    fun waitForEvidence(marker: String, packetName: String, value: String) {
        assert.waitUntil("evidence $marker/$packetName/$value") {
            snapshot().entries.any { it.marker == marker && it.packetName == packetName && it.value == value }
        }
    }

    fun waitForEvidenceValueSuffix(marker: String, packetName: String, suffix: String) {
        assert.waitUntil("evidence $marker/$packetName/*$suffix") {
            snapshot().entries.any { it.marker == marker && it.packetName == packetName && it.value.endsWith(suffix) }
        }
    }

    fun waitForFilterOrder(value: String) {
        val expected = listOf(
            "first-before:$value",
            "second-before:$value",
            "second-after:$value",
            "first-after:$value",
        )
        assert.waitUntil("RC-A5 filter order") {
            snapshot().entries
                .filter { it.marker == "Filter" && it.packetName == "EchoManual" && it.value.endsWith(":$value") }
                .map { it.value } == expected
        }
    }

    private fun snapshot(): EvidenceSnapshot {
        try {
            val request = HttpRequest.newBuilder(URI.create("$httpEndpoint/evidence"))
                .timeout(Duration.ofSeconds(3))
                .GET()
                .build()
            val response = http.send(request, HttpResponse.BodyHandlers.ofString())
            return json.readValue(response.body(), EvidenceSnapshot::class.java)
        } catch (error: Exception) {
            throw IllegalStateException("failed to fetch evidence", error)
        }
    }
}
