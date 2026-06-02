package systems.zlink.samples.tictactoe.client;

import java.util.List;

public record TicTacToeClientResult(String winner, List<String> pushes) {
}
