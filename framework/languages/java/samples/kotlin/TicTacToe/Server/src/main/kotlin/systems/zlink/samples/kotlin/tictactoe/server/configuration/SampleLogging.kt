package systems.zlink.samples.kotlin.tictactoe.server.configuration

import java.nio.file.Files
import java.nio.file.Path

object SampleLogging {
    fun configure(settings: SampleSettings, role: String) {
        Files.createDirectories(Path.of(settings.logDirectory))
        Path.of(settings.logDirectory, "$role.log").toFile().createNewFile()
        Files.createDirectories(Path.of(flowLogDirectory()))
    }

    fun flowLogPath(role: String): String =
        Path.of(flowLogDirectory(), "flow-$role.log").toString()

    private fun flowLogDirectory(): String =
        System.getenv("TICTACTOE_LOG_DIR") ?: "logs"
}
