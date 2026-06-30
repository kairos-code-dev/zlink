package systems.zlink.e2e.pubsub.registry;

public final class Program {
    private Program() {
    }

    public static void main(String[] args) {
        new RegistryApplication().run(args);
    }
}
