package systems.zlink.e2e.registrationcodec;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.protobuf.StringValue;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Duration;
import systems.zlink.framework.channels.ZLinkClient;

public final class ClientScenario {
    private static final Duration REQUEST_TIMEOUT = Duration.ofSeconds(5);
    private final ZLinkClient client;
    private final ObjectMapper json;
    private final HttpClient http = HttpClient.newHttpClient();

    public ClientScenario(ZLinkClient client, ObjectMapper json) {
        this.client = client;
        this.json = json;
    }

    public void run() {
        runRegistrationVariants();
        runCodecVariants();
    }

    private void runRegistrationVariants() {
        Contracts.EchoReply auto = client.requestToChannel(
                Contracts.CHANNEL,
                new Contracts.EchoAutoRequest("auto-request"))
            .packetName("EchoAuto")
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.EchoReply.class);
        ensure("echo:auto-request".equals(auto.value()) && "auto".equals(auto.handler()),
            "RC-A1 request mismatch");
        client.sendToChannel(Contracts.CHANNEL, new Contracts.EchoAutoCommand("auto-send"))
            .packetName("EchoAuto")
            .await();
        waitForEvidence("Send", "EchoAuto", "auto-send");
        System.out.println("scenario RC-A1 passed");

        Contracts.EchoReply attr = client.requestToChannel(
                Contracts.CHANNEL,
                new Contracts.EchoAttrRequest("attr-request"))
            .packetName("EchoAttr")
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.EchoReply.class);
        ensure("echo:attr-request".equals(attr.value()) && "attr".equals(attr.handler()),
            "RC-A2 request mismatch");
        client.sendToChannel(Contracts.CHANNEL, new Contracts.EchoAttrCommand("attr-send"))
            .packetName("EchoAttr")
            .await();
        waitForEvidence("Send", "EchoAttr", "attr-send");
        System.out.println("scenario RC-A2 passed");

        Contracts.EchoReply manual = client.requestToChannel(
                Contracts.CHANNEL,
                new Contracts.EchoManualRequest("manual-request"))
            .packetName("EchoManual")
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.EchoReply.class);
        ensure("echo:manual-request".equals(manual.value()) && "manual".equals(manual.handler()),
            "RC-A3 request mismatch");
        client.sendToChannel(Contracts.CHANNEL, new Contracts.EchoManualCommand("manual-send"))
            .packetName("EchoManual")
            .await();
        waitForEvidence("Send", "EchoManual", "manual-send");
        System.out.println("scenario RC-A3 passed");

        Contracts.EchoReply filtered = client.requestToChannel(
                Contracts.CHANNEL,
                new Contracts.EchoManualRequest("filter-order-request"))
            .packetName("EchoManual")
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.EchoReply.class);
        ensure("echo:filter-order-request".equals(filtered.value()), "RC-A5 request mismatch");
        waitForFilterOrder("filter-order-request");
        System.out.println("scenario RC-A5 passed");
    }

    private void runCodecVariants() {
        Contracts.EchoReply jsonReply = client.requestToChannel(
                Contracts.CHANNEL,
                new Contracts.JsonEchoRequest("json-request"))
            .packetName("JsonEcho")
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.EchoReply.class);
        ensure("echo:json-request".equals(jsonReply.value()) && "json".equals(jsonReply.handler()),
            "RC-B1 request mismatch");
        client.sendToChannel(Contracts.CHANNEL, new Contracts.JsonEchoCommand("json-send"))
            .packetName("JsonEcho")
            .await();
        waitForEvidence("Send", "JsonEcho", "json-send");
        System.out.println("scenario RC-B1 passed");

        StringValue protobufReply = client.requestToChannel(
                Contracts.CHANNEL,
                StringValue.of("protobuf-request"))
            .packetName("ProtobufEcho")
            .timeout(REQUEST_TIMEOUT)
            .await(StringValue.class);
        ensure("echo:protobuf-request".equals(protobufReply.getValue()),
            "RC-B2 request mismatch");
        client.sendToChannel(Contracts.CHANNEL, StringValue.of("protobuf-send"))
            .packetName("ProtobufEcho")
            .await();
        waitForEvidence("Send", "ProtobufEcho", "protobuf-send");
        System.out.println("scenario RC-B2 passed");

        Contracts.PackedEchoReply packedReply = client.requestToChannel(
                Contracts.CHANNEL,
                new Contracts.PackedEchoRequest("msgpack-request"))
            .packetName("MsgpackEcho")
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.PackedEchoReply.class);
        ensure("echo:msgpack-request".equals(packedReply.value()),
            "RC-B3 request mismatch");
        client.sendToChannel(Contracts.CHANNEL, new Contracts.PackedEchoCommand("msgpack-send"))
            .packetName("MsgpackEcho")
            .await();
        waitForEvidence("Send", "MsgpackEcho", "msgpack-send");
        System.out.println("scenario RC-B3 passed");
        System.out.println("scenario RC-B4 passed");
    }

    private void waitForEvidence(String marker, String packetName, String value) {
        long deadline = System.nanoTime() + Duration.ofSeconds(10).toNanos();
        while (System.nanoTime() < deadline) {
            if (snapshot().entries().stream().anyMatch(entry ->
                marker.equals(entry.marker())
                    && packetName.equals(entry.packetName())
                    && value.equals(entry.value()))) {
                return;
            }
            sleep(100);
        }
        throw new IllegalStateException(
            "timed out waiting for evidence " + marker + "/" + packetName + "/" + value);
    }

    private Contracts.EvidenceSnapshot snapshot() {
        try {
            HttpRequest request = HttpRequest.newBuilder(
                    URI.create(Env.get("ZLINK_JAVA_E2E_HTTP_ENDPOINT") + "/evidence"))
                .timeout(Duration.ofSeconds(3))
                .GET()
                .build();
            HttpResponse<String> response = http.send(request, HttpResponse.BodyHandlers.ofString());
            return json.readValue(response.body(), Contracts.EvidenceSnapshot.class);
        } catch (Exception error) {
            throw new IllegalStateException("failed to fetch evidence", error);
        }
    }

    private void waitForFilterOrder(String value) {
        long deadline = System.nanoTime() + Duration.ofSeconds(10).toNanos();
        while (System.nanoTime() < deadline) {
            var entries = snapshot().entries().stream()
                .filter(entry -> "Filter".equals(entry.marker()))
                .filter(entry -> "EchoManual".equals(entry.packetName()))
                .map(Contracts.EvidenceEntry::value)
                .filter(entry -> entry.endsWith(":" + value))
                .toList();
            if (entries.equals(java.util.List.of(
                "first-before:" + value,
                "second-before:" + value,
                "second-after:" + value,
                "first-after:" + value))) {
                return;
            }
            sleep(100);
        }
        throw new IllegalStateException("timed out waiting for RC-A5 filter order");
    }

    private static void sleep(long millis) {
        try {
            Thread.sleep(millis);
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("interrupted", error);
        }
    }

    private static void ensure(boolean condition, String message) {
        if (!condition) {
            throw new IllegalStateException(message);
        }
    }
}
