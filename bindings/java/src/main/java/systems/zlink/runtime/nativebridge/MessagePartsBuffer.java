/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.nativebridge;

import systems.zlink.contracts.Message;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

public final class MessagePartsBuffer {
    private Message singlePart;
    private ArrayList<Message> parts;
    private List<Message> view;

    public void add(Message part) {
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

    public boolean isEmpty() {
        return singlePart == null && (parts == null || parts.isEmpty());
    }

    public int size() {
        return parts != null ? parts.size() : singlePart == null ? 0 : 1;
    }

    public Message get(int index) {
        if (parts != null)
            return parts.get(index);
        if (index == 0 && singlePart != null)
            return singlePart;
        throw new IndexOutOfBoundsException(index);
    }

    public List<Message> asList() {
        if (parts != null)
            return parts;
        if (view == null)
            view = singlePart == null ? List.of() : List.of(singlePart);
        return view;
    }
}
