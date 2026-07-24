package systems.zlink.framework.locations;

import java.util.Objects;

public record ZLinkCreationTerminalFound(
    ZLinkCreationOperationTerminal terminal)
    implements ZLinkCreationTerminalReadResult {
    public ZLinkCreationTerminalFound {
        Objects.requireNonNull(terminal, "terminal");
    }
}
