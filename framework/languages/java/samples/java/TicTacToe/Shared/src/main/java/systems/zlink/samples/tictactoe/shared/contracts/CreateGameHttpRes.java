package systems.zlink.samples.tictactoe.shared.contracts;

import java.util.List;

public record CreateGameHttpRes(
    String roomId,
    String gameName,
    String ownerPlayEndpoint,
    List<String> playEndpoints,
    List<PlayNodeInfo> playNodes,
    int requiredLevel) {
}
