package systems.zlink.samples.tictactoe.client;

import java.io.PrintStream;
import java.util.List;

public record TicTacToeClientResult(String winner, List<String> pushes) {
    public void writeTo(PrintStream output) {
        output.println("winner=" + winner);
        output.println("pushes=" + String.join(",", pushes));
    }
}
