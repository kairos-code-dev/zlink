/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.sockets;

import systems.zlink.contracts.sockets.*;

import systems.zlink.contracts.messaging.Message;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

final class MessageParts {
    private Message singlePart;
    private ArrayList<Message> parts;
    private List<Message> view;

    void add(Message part) {
        Objects.requireNonNull(part, "part");
        view = null;
        if (parts != null) {
            parts.add(part);
            return;
        }
        if (singlePart == null) {
            singlePart = part;
            return;
        }
        parts = new ArrayList<>(4);
        parts.add(singlePart);
        parts.add(part);
        singlePart = null;
    }

    boolean isEmpty() {
        return singlePart == null && (parts == null || parts.isEmpty());
    }

    List<Message> asList() {
        if (parts != null)
            return parts;
        if (view == null)
            view = singlePart == null ? List.of() : List.of(singlePart);
        return view;
    }
}
