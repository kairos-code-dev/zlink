package systems.zlink.samples.bingo.server.api;

public final class Program {
    private Program() {
    }

    public static void main(String[] args) {
        ApiServerHostFactory.start(args);
    }
}
