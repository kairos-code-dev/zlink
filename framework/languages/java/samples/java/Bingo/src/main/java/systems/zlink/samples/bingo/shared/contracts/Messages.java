package systems.zlink.samples.bingo.shared.contracts;

public final class Messages {
    private Messages() {
    }

    public record BingoWinner(String payload) {
    }
}
