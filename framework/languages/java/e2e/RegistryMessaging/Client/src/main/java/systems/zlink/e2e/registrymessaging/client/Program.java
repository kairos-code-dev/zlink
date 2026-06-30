package systems.zlink.e2e.registrymessaging.client;

import systems.zlink.e2e.registrymessaging.client.Support.RegistryMessagingHttp;
import systems.zlink.e2e.registrymessaging.client.Support.ScenarioCatalog;

public final class Program {
    private Program() {
    }

    public static void main(String[] args) {
        try (RegistryMessagingHttp http = new RegistryMessagingHttp()) {
            new ScenarioCatalog(http).run();
        }
        System.out.println("registry-messaging e2e result=passed");
    }
}
