package systems.zlink.e2e.runtimemonitoring.throwingservice;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.builder.SpringApplicationBuilder;
import systems.zlink.e2e.runtimemonitoring.shared.Env;

public final class Program {
    private Program() {
    }

    public static void main(String... args) {
        Env.configure(args);
        SpringApplicationBuilder builder = new SpringApplicationBuilder(
            systems.zlink.e2e.runtimemonitoring.service.Program.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        builder.run();
    }
}
