package systems.zlink.samples.kotlin.deliverydispatch.server.configuration

import java.nio.charset.StandardCharsets
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.Paths
import java.nio.file.StandardOpenOption
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatusChanged

class EvidenceStore {
    private val path: Path
    private val gate = Any()

    init {
        val directory = System.getProperty(
            "zlink.samples.deliverydispatch.workDir",
            System.getenv("DELIVERYDISPATCH_WORK_DIR")
                ?: Paths.get(System.getProperty("java.io.tmpdir"), "zlink-deliverydispatch").toString(),
        )
        val dir = Paths.get(directory)
        Files.createDirectories(dir)
        path = dir.resolve("events.log")
    }

    fun append(status: DeliveryStatusChanged) {
        val line = listOf(
            status.deliveryId,
            status.status,
            status.courierId ?: "",
            status.occurredAtUnixMs.toString(),
        ).joinToString("|")
        synchronized(gate) {
            Files.write(
                path,
                listOf(line),
                StandardCharsets.UTF_8,
                StandardOpenOption.CREATE,
                StandardOpenOption.APPEND,
            )
        }
    }

    fun readLines(): List<String> =
        synchronized(gate) {
            if (!Files.exists(path)) {
                emptyList()
            } else {
                Files.readAllLines(path, StandardCharsets.UTF_8)
            }
        }

    fun hasSequence(deliveryId: String, vararg expected: String): Boolean {
        val statuses = readLines()
            .map { it.split("|") }
            .filter { it.size >= 2 && it[0] == deliveryId }
            .map { it[1] }
        return statuses == expected.toList()
    }
}
