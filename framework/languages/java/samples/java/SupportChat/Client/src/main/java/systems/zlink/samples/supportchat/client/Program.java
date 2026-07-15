package systems.zlink.samples.supportchat.client;

import java.net.URI;
import java.time.Duration;
import java.util.Arrays;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.TimeoutException;
import systems.zlink.samples.supportchat.server.configuration.SampleNames;
import systems.zlink.samples.supportchat.server.configuration.SampleTimings;
import systems.zlink.samples.supportchat.shared.contracts.Messages;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory;
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions;
import systems.zlink.stream.connector.ZLinkStreamDispatchMode;
import systems.zlink.stream.connector.ZLinkStreamMessage;

public final class Program {
    private Program() {
    }

    public static void main(String[] args) {
        ClientOptions options = ClientOptions.parse(args);
        ZLinkStreamConnector[] clients = new ZLinkStreamConnector[6];
        Arrays.setAll(clients, ignored -> createClient(options));
        try {
            new SupportChatClientScenario().run(
                clients[0], clients[1], clients[2], clients[3], clients[4], clients[5]);
            System.out.println(SampleNames.ClientMarker);
        } finally {
            for (ZLinkStreamConnector client : clients) {
                try {
                    client.close().submit().toCompletableFuture().join();
                } catch (RuntimeException ignored) {
                }
            }
        }
    }

    private static ZLinkStreamConnector createClient(ClientOptions options) {
        return ZLinkStreamConnectorFactory.create(new ZLinkStreamConnectorOptions(
            options.streamEndpoint(),
            ZLinkStreamDispatchMode.IMMEDIATE,
            SampleTimings.RequestTimeout,
            SampleTimings.RequestTimeout,
            2,
            SampleTimings.ConnectTimeout,
            64 * 1024,
            64 * 1024,
            Integer.MAX_VALUE,
            true,
            Duration.ofSeconds(1),
            SampleTimings.RequestTimeout.plusSeconds(5),
            true,
            Duration.ofMillis(250),
            Duration.ofSeconds(5),
            2.0,
            false,
            null,
            null,
            null,
            null));
    }

    private record ClientOptions(URI streamEndpoint) {
        static ClientOptions parse(String[] args) {
            if (args.length != 2 || !"--stream-endpoint".equals(args[0])) {
                throw new IllegalArgumentException("Usage: Client --stream-endpoint <tcp://host:port>");
            }
            URI endpoint = URI.create(args[1]);
            if (!"tcp".equals(endpoint.getScheme())
                || endpoint.getHost() == null
                || endpoint.getPort() < 1
                || endpoint.getPort() > 65535) {
                throw new IllegalArgumentException("--stream-endpoint must be a valid tcp endpoint");
            }
            return new ClientOptions(endpoint);
        }
    }
}

final class SupportChatClientScenario {
    public void run(
        ZLinkStreamConnector agent,
        ZLinkStreamConnector customer1,
        ZLinkStreamConnector customer2,
        ZLinkStreamConnector reconnectingAgent,
        ZLinkStreamConnector reconnectingCustomer,
        ZLinkStreamConnector waitingCustomer) {
        connectAndAuthenticate(agent, "agent-1", SampleNames.Roles.Agent);
        ensure(setAvailability(agent, true).isAvailable());

        connectAndAuthenticate(customer1, "customer-1", SampleNames.Roles.Customer);
        CompletionStage<ZLinkStreamMessage<Messages.ConversationAssignedNotify>> assigned1 =
            waitFor(agent, Messages.ConversationAssignedNotify.class);
        Messages.OpenConversationRes opened1 = request(
            customer1, new Messages.OpenConversationReq("checkout payment failed"),
            Messages.OpenConversationRes.class);
        String conversation1 = opened1.conversationId();
        ensure(SampleNames.Statuses.WaitingForAgent.equals(opened1.state().status()));
        ensure(join(assigned1).payload().conversationId().equals(conversation1));

        ConversationClient agentRoom1 = new ConversationClient(agent, conversation1);
        ConversationClient customerRoom1 = new ConversationClient(customer1, conversation1);
        CompletionStage<ZLinkStreamMessage<Messages.ParticipantJoinedNotify>> joined1 =
            waitFor(customer1, Messages.ParticipantJoinedNotify.class);
        Messages.JoinConversationRes agentJoined1 = agentRoom1.join();
        ensure(SampleNames.Statuses.Active.equals(agentJoined1.state().status()));
        ensure("agent-1".equals(agentJoined1.state().agentActorId()));
        ensure("checkout payment failed".equals(agentJoined1.state().subject()));
        ensure(join(joined1).payload().conversationId().equals(conversation1));

        CompletionStage<ZLinkStreamMessage<Messages.ChatMessageNotify>> greeting1 =
            waitFor(customer1, Messages.ChatMessageNotify.class);
        ensure(agentRoom1.sendChat("How can I help?").message().messageSeq() == 1);
        ensure(join(greeting1).payload().message().messageSeq() == 1);

        CompletionStage<ZLinkStreamMessage<Messages.ChatMessageNotify>> reply1 =
            waitFor(agent, Messages.ChatMessageNotify.class);
        ensure(customerRoom1.sendChat("Payment keeps failing.").message().messageSeq() == 2);
        ensure(join(reply1).payload().message().messageSeq() == 2);

        connectAndAuthenticate(customer2, "customer-2", SampleNames.Roles.Customer);
        CompletionStage<ZLinkStreamMessage<Messages.ConversationAssignedNotify>> assigned2 =
            waitFor(agent, Messages.ConversationAssignedNotify.class);
        Messages.OpenConversationRes opened2 = request(
            customer2, new Messages.OpenConversationReq("cannot log in"),
            Messages.OpenConversationRes.class);
        String conversation2 = opened2.conversationId();
        ensure(!conversation2.equals(conversation1));
        ensure(SampleNames.Statuses.WaitingForAgent.equals(opened2.state().status()));
        ensure(join(assigned2).payload().conversationId().equals(conversation2));

        ConversationClient agentRoom2 = new ConversationClient(agent, conversation2);
        ConversationClient customerRoom2 = new ConversationClient(customer2, conversation2);
        CompletionStage<ZLinkStreamMessage<Messages.ParticipantJoinedNotify>> joined2 =
            waitFor(customer2, Messages.ParticipantJoinedNotify.class);
        ensure("cannot log in".equals(agentRoom2.join().state().subject()));
        ensure(join(joined2).payload().conversationId().equals(conversation2));
        CompletionStage<ZLinkStreamMessage<Messages.ChatMessageNotify>> greeting2 =
            waitFor(customer2, Messages.ChatMessageNotify.class);
        ensure(agentRoom2.sendChat("Let me check your account.").message().messageSeq() == 1);
        ensure(join(greeting2).payload().conversationId().equals(conversation2));

        CompletionStage<ZLinkStreamMessage<Messages.TypingChangedNotify>> typing1 =
            waitFor(customer1, Messages.TypingChangedNotify.class);
        agentRoom1.sendTyping(true);
        Messages.TypingChangedNotify typing = join(typing1).payload();
        ensure(typing.conversationId().equals(conversation1));
        ensure("agent-1".equals(typing.actorId()) && typing.isTyping());

        close(customer1);
        connectAndAuthenticate(reconnectingCustomer, "customer-1", SampleNames.Roles.Customer);
        customerRoom1 = new ConversationClient(reconnectingCustomer, conversation1);
        Messages.ConversationState customerRejoin = customerRoom1.join().state();
        ensure(SampleNames.Statuses.Active.equals(customerRejoin.status()));
        ensure(customerRejoin.lastMessageSeq() == 2);

        close(agent);
        connectAndAuthenticate(reconnectingAgent, "agent-1", SampleNames.Roles.Agent);
        ensure(setAvailability(reconnectingAgent, true).isAvailable());
        agentRoom1 = new ConversationClient(reconnectingAgent, conversation1);
        agentRoom2 = new ConversationClient(reconnectingAgent, conversation2);
        ensure("checkout payment failed".equals(agentRoom1.join().state().subject()));
        ensure("cannot log in".equals(agentRoom2.join().state().subject()));

        Duration lifecycleTimeout = SampleTimings.IdleTimeout
            .plus(SampleTimings.CloseGraceTimeout)
            .plus(SampleTimings.RequestTimeout);
        CompletionStage<ZLinkStreamMessage<Messages.ConversationIdleNotify>> idleCustomer1 =
            waitFor(reconnectingCustomer, Messages.ConversationIdleNotify.class, lifecycleTimeout, conversation1);
        CompletionStage<ZLinkStreamMessage<Messages.ConversationIdleNotify>> idleAgent1 =
            waitFor(reconnectingAgent, Messages.ConversationIdleNotify.class, lifecycleTimeout, conversation1);
        CompletionStage<ZLinkStreamMessage<Messages.ConversationClosedNotify>> closedCustomer1 =
            waitFor(reconnectingCustomer, Messages.ConversationClosedNotify.class, lifecycleTimeout, conversation1);
        CompletionStage<ZLinkStreamMessage<Messages.ConversationClosedNotify>> closedAgent1 =
            waitFor(reconnectingAgent, Messages.ConversationClosedNotify.class, lifecycleTimeout, conversation1);
        CompletionStage<ZLinkStreamMessage<Messages.ConversationClosedNotify>> closedAgent2 =
            waitFor(reconnectingAgent, Messages.ConversationClosedNotify.class, lifecycleTimeout, conversation2);

        ensure(SampleNames.Statuses.Closed.equals(customerRoom2.close("resolved").state().status()));
        ensure(join(closedAgent2).payload().conversationId().equals(conversation2));
        ConversationClient closedRoom2 = customerRoom2;
        expectFailure(() -> closedRoom2.close("again"));

        ensure(SampleNames.Statuses.WaitingForClose.equals(join(idleCustomer1).payload().state().status()));
        ensure(join(idleAgent1).payload().conversationId().equals(conversation1));
        ensure(SampleNames.Statuses.Closed.equals(join(closedCustomer1).payload().state().status()));
        ensure(join(closedAgent1).payload().conversationId().equals(conversation1));

        ConversationClient closedRoom1 = customerRoom1;
        expectFailure(() -> closedRoom1.sendChat("are you there?"));
        CompletionStage<ZLinkStreamMessage<Messages.TypingChangedNotify>> closedTyping =
            waitFor(reconnectingAgent, Messages.TypingChangedNotify.class,
                Duration.ofMillis(500), conversation1);
        closedRoom1.sendTyping(true);
        expectTimeout(closedTyping);
        System.out.println("supportchat-closed-typing-ignore=verified");

        ensure(!setAvailability(reconnectingAgent, false).isAvailable());
        connectAndAuthenticate(waitingCustomer, "customer-3", SampleNames.Roles.Customer);
        expectFailure(() -> setAvailability(waitingCustomer, true));
        Messages.OpenConversationRes waiting = request(
            waitingCustomer, new Messages.OpenConversationReq("agent unavailable"),
            Messages.OpenConversationRes.class);
        ensure(SampleNames.Statuses.WaitingForAgent.equals(waiting.state().status()));
        ensure("agent unavailable".equals(waiting.state().subject()));
        expectTimeout(waitingCustomer.waitFor(Messages.ConversationClosedNotify.class)
            .timeout(Duration.ofMillis(500))
            .submit(Messages.ConversationClosedNotify.class));
        System.out.println("supportchat-conversation=" + conversation1);
    }

    private static void connectAndAuthenticate(
        ZLinkStreamConnector connector,
        String token,
        String role) {
        join(connector.connect().submit());
        Messages.AuthenticateRes authenticated = request(
            connector, new Messages.AuthenticateReq(token), Messages.AuthenticateRes.class);
        ensure(token.equals(authenticated.actorId()));
        ensure(role.equals(authenticated.role()));
    }

    private static Messages.SetAgentAvailableRes setAvailability(
        ZLinkStreamConnector connector,
        boolean available) {
        return request(connector, new Messages.SetAgentAvailableReq(available),
            Messages.SetAgentAvailableRes.class);
    }

    private static <T> T request(ZLinkStreamConnector connector, Object request, Class<T> type) {
        return join(connector.request(request).submit(type));
    }

    private static <T> CompletionStage<ZLinkStreamMessage<T>> waitFor(
        ZLinkStreamConnector connector,
        Class<T> type) {
        return connector.waitFor(type).submit(type);
    }

    private static <T> CompletionStage<ZLinkStreamMessage<T>> waitFor(
        ZLinkStreamConnector connector,
        Class<T> type,
        Duration timeout,
        String conversationId) {
        return connector.waitFor(type)
            .where(type, message -> message.payload() instanceof Messages.ConversationIdleNotify idle
                ? idle.conversationId().equals(conversationId)
                : ((Messages.ConversationClosedNotify) message.payload()).conversationId().equals(conversationId))
            .timeout(timeout)
            .submit(type);
    }

    private static void close(ZLinkStreamConnector connector) {
        join(connector.close().submit());
    }

    private static void expectFailure(Runnable action) {
        try {
            action.run();
        } catch (RuntimeException expected) {
            return;
        }
        throw new IllegalStateException("Expected request failure");
    }

    private static void expectTimeout(CompletionStage<?> stage) {
        try {
            join(stage);
        } catch (RuntimeException error) {
            Throwable cause = error;
            while (cause.getCause() != null) {
                cause = cause.getCause();
            }
            if (cause instanceof TimeoutException) {
                return;
            }
            throw error;
        }
        throw new IllegalStateException("Expected wait timeout");
    }

    private static <T> T join(CompletionStage<T> stage) {
        try {
            return stage.toCompletableFuture().join();
        } catch (CompletionException error) {
            throw error;
        }
    }

    private static void ensure(boolean condition) {
        if (!condition) {
            throw new IllegalStateException("SupportChat client self-check failed");
        }
    }

    private static final class ConversationClient {
        private final ZLinkStreamConnector connector;
        private final String conversationId;

        private ConversationClient(ZLinkStreamConnector connector, String conversationId) {
            this.connector = connector;
            this.conversationId = conversationId;
        }

        private Messages.JoinConversationRes join() {
            return SupportChatClientScenario.join(connector.request(new Messages.JoinConversationReq())
                .metadata(SampleNames.ConversationIdMetadataKey, conversationId)
                .submit(Messages.JoinConversationRes.class));
        }

        private Messages.SendChatMessageRes sendChat(String text) {
            return SupportChatClientScenario.join(connector.request(new Messages.SendChatMessageReq(text))
                .metadata(SampleNames.ConversationIdMetadataKey, conversationId)
                .submit(Messages.SendChatMessageRes.class));
        }

        private void sendTyping(boolean typing) {
            connector.send(new Messages.SetTypingReq(typing))
                .metadata(SampleNames.ConversationIdMetadataKey, conversationId)
                .submit();
        }

        private Messages.CloseConversationRes close(String reason) {
            return SupportChatClientScenario.join(connector.request(new Messages.CloseConversationReq(reason))
                .metadata(SampleNames.ConversationIdMetadataKey, conversationId)
                .submit(Messages.CloseConversationRes.class));
        }
    }
}
