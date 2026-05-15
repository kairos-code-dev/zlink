/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;

final class LegacyReceiveState {
    private Message[] frames = new Message[0];
    private int index;

    boolean hasPending() {
        return index < frames.length;
    }

    int pendingCount() {
        return frames.length - index;
    }

    Message poll() {
        Message frame = frames[index++];
        frame.setMore(index < frames.length);
        if (!hasPending()) {
            frames = new Message[0];
            index = 0;
        }
        return frame;
    }

    void replace(Message[] nextFrames) {
        closeRemaining();
        frames = nextFrames;
        index = 0;
    }

    void closeRemaining() {
        for (int i = index; i < frames.length; i++) {
            if (frames[i] != null) {
                try {
                    frames[i].close();
                } catch (RuntimeException ignored) {
                }
            }
        }
        frames = new Message[0];
        index = 0;
    }
}
