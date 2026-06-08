package systems.zlink.samples.bingo.client;

public final class Program {
    private Program() {
    }

    public static void main(String[] args) throws Exception {
        new BingoClientApp(new BingoClientOptions(2)).run();
        System.out.println("Bingo client self-check passed");
    }
}
