package systems.zlink.samples.bingo.client;

import java.util.ArrayList;
import java.util.List;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class BingoNotificationInbox {
    private final List<Messages.PlayerJoinedNotify> playerJoined = new ArrayList<>();
    private final List<Messages.BingoGameStartedNotify> started = new ArrayList<>();
    private final List<Messages.BingoNumberDrawnNotify> drawn = new ArrayList<>();
    private final List<Messages.BingoGameEndedNotify> ended = new ArrayList<>();

    public void playerJoined(Messages.PlayerJoinedNotify value) {
        playerJoined.add(value);
    }

    public void started(Messages.BingoGameStartedNotify value) {
        started.add(value);
    }

    public void drawn(Messages.BingoNumberDrawnNotify value) {
        drawn.add(value);
    }

    public void ended(Messages.BingoGameEndedNotify value) {
        ended.add(value);
    }

    public List<Messages.PlayerJoinedNotify> playerJoined() {
        return List.copyOf(playerJoined);
    }

    public List<Messages.BingoGameStartedNotify> started() {
        return List.copyOf(started);
    }

    public List<Messages.BingoNumberDrawnNotify> drawn() {
        return List.copyOf(drawn);
    }

    public List<Messages.BingoGameEndedNotify> ended() {
        return List.copyOf(ended);
    }

    public List<Messages.BingoWinner> winners() {
        if (ended.isEmpty()) {
            return List.of();
        }
        return ended.getLast().state().winners().stream()
            .map(Messages.BingoWinner::new)
            .toList();
    }
}
