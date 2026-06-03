package systems.zlink.samples.kotlin.tictactoe.client

import java.io.PrintStream

data class TicTacToeClientResult(
    val winner: String?,
    val pushes: List<String>,
) {
    fun writeTo(output: PrintStream) {
        output.println("winner=$winner")
        output.println("pushes=${pushes.joinToString(",")}")
    }
}
