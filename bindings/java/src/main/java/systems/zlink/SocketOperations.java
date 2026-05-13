/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink;

import systems.zlink.service.spot.ReplyOp;
import systems.zlink.service.spot.ReplySubmitOp;
import systems.zlink.service.spot.RequestCallbackSubmitOp;
import systems.zlink.service.spot.RequestOp;
import systems.zlink.service.spot.RequestSubmitOp;
import systems.zlink.service.spot.SendOp;
import systems.zlink.service.spot.SendSubmitOp;
import systems.zlink.internal.MessagePartsBuffer;
import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;

final class SocketOperations {
    private SocketOperations() {
    }

    static SendOp send(SendInvoker invoker) {
        return new SendBuilder(invoker);
    }

    static RequestOp request(RequestAsyncInvoker asyncInvoker,
                             RequestCallbackInvoker callbackInvoker) {
        return new RequestBuilder(asyncInvoker, callbackInvoker);
    }

    static ReplyOp reply(ReplyInvoker invoker) {
        return new ReplyBuilder(invoker);
    }

    @FunctionalInterface
    interface SendInvoker {
        boolean submit(List<Message> parts, SendFlags flags);
    }

    @FunctionalInterface
    interface RequestAsyncInvoker {
        CompletableFuture<List<Message>> submit(List<Message> parts,
                                                SendFlags flags,
                                                Duration timeout);
    }

    @FunctionalInterface
    interface RequestCallbackInvoker {
        boolean submit(List<Message> parts, RequestCallback callback,
                       SendFlags flags, Duration timeout);
    }

    @FunctionalInterface
    interface ReplyInvoker {
        void submit(List<Message> parts, SendFlags flags);
    }

    private static final class SendBuilder implements SendOp, SendSubmitOp {
        private final SendInvoker invoker;
        private final MessagePartsBuffer parts = new MessagePartsBuffer();
        private SendFlags flags = SendFlags.NONE;
        private boolean submitted;

        private SendBuilder(SendInvoker invoker) {
            this.invoker = Objects.requireNonNull(invoker, "invoker");
        }

        @Override
        public SendSubmitOp message(Message part) {
            ensureNotSubmitted();
            parts.add(Objects.requireNonNull(part, "part"));
            return this;
        }

        @Override
        public SendSubmitOp flags(SendFlags value) {
            ensureNotSubmitted();
            flags = Objects.requireNonNull(value, "flags");
            return this;
        }

        @Override
        public boolean submit() {
            markSubmitted();
            return invoker.submit(parts.asList(), flags);
        }

        private void markSubmitted() {
            ensureNotSubmitted();
            if (parts.isEmpty())
                throw new IllegalArgumentException("at least one message required");
            submitted = true;
        }

        private void ensureNotSubmitted() {
            if (submitted)
                throw new IllegalStateException("operation already submitted");
        }
    }

    private static final class RequestBuilder
      implements RequestOp, RequestSubmitOp {
        private final RequestAsyncInvoker asyncInvoker;
        private final RequestCallbackInvoker callbackInvoker;
        private final MessagePartsBuffer parts = new MessagePartsBuffer();
        private Duration timeout =
          Duration.ofMillis(RequestReplySupport.DEFAULT_TIMEOUT_MS);
        private SendFlags flags = SendFlags.NONE;
        private boolean submitted;

        private RequestBuilder(RequestAsyncInvoker asyncInvoker,
                               RequestCallbackInvoker callbackInvoker) {
            this.asyncInvoker = Objects.requireNonNull(asyncInvoker,
              "asyncInvoker");
            this.callbackInvoker = Objects.requireNonNull(callbackInvoker,
              "callbackInvoker");
        }

        @Override
        public RequestSubmitOp message(Message part) {
            addMessage(part);
            return this;
        }

        @Override
        public RequestSubmitOp timeout(Duration value) {
            ensureNotSubmitted();
            timeout = Objects.requireNonNull(value, "timeout");
            return this;
        }

        @Override
        public RequestCallbackSubmitOp flags(SendFlags value) {
            ensureNotSubmitted();
            return new CallbackRequestBuilder(this,
              Objects.requireNonNull(value, "flags"));
        }

        @Override
        public CompletableFuture<List<Message>> submitAsync() {
            markSubmitted();
            return asyncInvoker.submit(parts.asList(), flags, timeout);
        }

        @Override
        public boolean submit(RequestCallback callback) {
            markSubmitted();
            return callbackInvoker.submit(parts.asList(),
              Objects.requireNonNull(callback, "callback"), flags, timeout);
        }

        private void addMessage(Message part) {
            ensureNotSubmitted();
            parts.add(Objects.requireNonNull(part, "part"));
        }

        private void markSubmitted() {
            ensureNotSubmitted();
            if (parts.isEmpty())
                throw new IllegalArgumentException("at least one message required");
            submitted = true;
        }

        private void ensureNotSubmitted() {
            if (submitted)
                throw new IllegalStateException("operation already submitted");
        }
    }

    private static final class CallbackRequestBuilder
      implements RequestCallbackSubmitOp {
        private final RequestBuilder source;
        private SendFlags flags;

        private CallbackRequestBuilder(RequestBuilder source,
                                       SendFlags flags) {
            this.source = source;
            this.flags = flags;
        }

        @Override
        public RequestCallbackSubmitOp message(Message part) {
            source.addMessage(part);
            return this;
        }

        @Override
        public RequestCallbackSubmitOp timeout(Duration value) {
            source.timeout(value);
            return this;
        }

        @Override
        public RequestCallbackSubmitOp flags(SendFlags value) {
            source.ensureNotSubmitted();
            flags = Objects.requireNonNull(value, "flags");
            return this;
        }

        @Override
        public boolean submit(RequestCallback callback) {
            source.markSubmitted();
            return source.callbackInvoker.submit(source.parts.asList(),
              Objects.requireNonNull(callback, "callback"), flags,
              source.timeout);
        }
    }

    private static final class ReplyBuilder implements ReplyOp, ReplySubmitOp {
        private final ReplyInvoker invoker;
        private final MessagePartsBuffer parts = new MessagePartsBuffer();
        private SendFlags flags = SendFlags.NONE;
        private boolean submitted;

        private ReplyBuilder(ReplyInvoker invoker) {
            this.invoker = Objects.requireNonNull(invoker, "invoker");
        }

        @Override
        public ReplySubmitOp message(Message part) {
            ensureNotSubmitted();
            parts.add(Objects.requireNonNull(part, "part"));
            return this;
        }

        @Override
        public ReplySubmitOp flags(SendFlags value) {
            ensureNotSubmitted();
            flags = Objects.requireNonNull(value, "flags");
            return this;
        }

        @Override
        public void submit() {
            ensureNotSubmitted();
            if (parts.isEmpty())
                throw new IllegalArgumentException("at least one message required");
            submitted = true;
            invoker.submit(parts.asList(), flags);
        }

        private void ensureNotSubmitted() {
            if (submitted)
                throw new IllegalStateException("operation already submitted");
        }
    }
}
