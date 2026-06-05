package systems.zlink.samples.tictactoe.server;

import org.springframework.context.ConfigurableApplicationContext;
import systems.zlink.samples.tictactoe.server.api.ApiServerHostFactory;
import systems.zlink.samples.tictactoe.server.configuration.SampleSettings;
import systems.zlink.samples.tictactoe.server.play.PlayServerHostFactory;

public final class TicTacToeServerHostFactory {
    private TicTacToeServerHostFactory() {
    }

    public static AutoCloseable start(SampleSettings settings) {
        SampleSettings.setCurrent(settings);
        ConfigurableApplicationContext play = null;
        ConfigurableApplicationContext api = null;
        try {
            play = PlayServerHostFactory.start(settings);
            api = ApiServerHostFactory.start(settings);
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
            TicTacToeServerHostFactory.close(api);
            TicTacToeServerHostFactory.close(play);
        }
    }

    private static void close(ConfigurableApplicationContext context) {
        if (context != null) {
            context.close();
        }
    }
}
