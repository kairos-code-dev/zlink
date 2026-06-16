package systems.zlink.samples.shoppingmallcheckout.server.commerceapi;

import java.util.concurrent.locks.LockSupport;
import org.springframework.stereotype.Component;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.samples.shoppingmallcheckout.server.configuration.SampleNames;
import systems.zlink.samples.shoppingmallcheckout.server.configuration.SampleTimings;
import systems.zlink.samples.shoppingmallcheckout.server.configuration.SampleTopology;
import systems.zlink.samples.shoppingmallcheckout.shared.contracts.Messages;
import systems.zlink.samples.shoppingmallcheckout.shared.contracts.Messages.OrderState;

/**
 * Routes workflow commands from {@code CommerceApi} to the {@code OrderWorkflow}
 * owner for an {@code OrderId}. Same {@code OrderId} always reaches the same
 * workflow instance channel.
 */
@Component
public final class OrderWorkflowRouter {
    private final ZLinkClient channels;

    public OrderWorkflowRouter(ZLinkClient channels) {
        this.channels = channels;
    }

    public OrderState startWorkflow(Messages.StartOrderWorkflowReq command) {
        Messages.StartOrderWorkflowRes response = request(
            command.orderId(), command, Messages.StartOrderWorkflowRes.class);
        return response.state();
    }

    public OrderState continueWorkflow(String orderId) {
        Messages.ContinueOrderWorkflowRes response = request(
            orderId, new Messages.ContinueOrderWorkflowReq(orderId), Messages.ContinueOrderWorkflowRes.class);
        return response.state();
    }

    public OrderState rebuildProjection(String orderId) {
        Messages.RebuildOrderProjectionRes response = request(
            orderId, new Messages.RebuildOrderProjectionReq(orderId), Messages.RebuildOrderProjectionRes.class);
        return response.state();
    }

    private <TReply> TReply request(String orderId, Object payload, Class<TReply> replyType) {
        String channel = SampleNames.workflowChannel(
            SampleTopology.workflowInstanceForOrder(orderId));
        RuntimeException lastError = null;
        for (int attempt = 1; attempt <= SampleTimings.MaxChannelAttempts; attempt++) {
            try {
                return channels.requestToChannel(channel, payload)
                    .timeout(SampleTimings.WorkflowTimeout)
                    .await(replyType);
            } catch (RuntimeException error) {
                lastError = error;
                LockSupport.parkNanos(SampleTimings.ChannelRetryDelay.toNanos());
            }
        }
        throw new IllegalStateException(
            "OrderWorkflow channel was not ready for order '" + orderId + "'.", lastError);
    }
}
