package systems.zlink.samples.bingo.client;

import java.util.List;

public final class Program {
    private Program() {
    }

    public static void main(String[] args) throws Exception {
        new BingoClientApp(new BingoClientOptions(4)).run(List.of(7, 11, 42, 42));
        System.out.println("Bingo client self-check passed");
    }
}
