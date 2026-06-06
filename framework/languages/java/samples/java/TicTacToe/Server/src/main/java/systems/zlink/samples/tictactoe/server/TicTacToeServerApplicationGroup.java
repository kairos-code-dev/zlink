package systems.zlink.samples.tictactoe.server;

import org.springframework.context.ConfigurableApplicationContext;
import systems.zlink.samples.tictactoe.server.api.ApiServerApplication;
import systems.zlink.samples.tictactoe.server.configuration.SampleSettings;
import systems.zlink.samples.tictactoe.server.play.PlayServerApplication;

public final class TicTacToeServerApplicationGroup {
    private TicTacToeServerApplicationGroup() {
    }

    public static AutoCloseable run(SampleSettings settings) {
        ConfigurableApplicationContext play = null;
        ConfigurableApplicationContext api = null;
        try {
            play = PlayServerApplication.run(settings);
            api = ApiServerApplication.run(settings);
            return new ServerHost(play, api);
        } catch (RuntimeException error) {
            close(api);
            close(play);
            throw error;
        }
    }

    private record ServerHost(
        ConfigurableApplicationContext play,
        ConfigurableApplicationContext api) implements AutoCloseable {
        @Override
        public void close() {
            TicTacToeServerApplicationGroup.close(api);
            TicTacToeServerApplicationGroup.close(play);
        }
    }

    private static void close(ConfigurableApplicationContext context) {
        if (context != null) {
            context.close();
        }
    }
}
