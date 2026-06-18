package systems.zlink.samples.shoppingmall.server.orderworkflow;

import jakarta.annotation.PostConstruct;
import jakarta.annotation.PreDestroy;
import java.util.concurrent.atomic.AtomicBoolean;
import org.springframework.stereotype.Component;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages.OrderState;

/**
 * Drives the asynchronous part of the checkout saga. Workflow handlers reply
 * to {@code StartOrderWorkflowReq} as soon as the {@code Created} projection
 * exists; this worker then advances inventory, payment, and confirmation in the
 * background so the client observes progress through {@code GetOrderStateReq}.
 */
@Component
public final class WorkflowSagaWorker {
    private final WorkflowContinuationQueue queue;
    private final OrderWorkflowService workflow;
    private final AtomicBoolean running = new AtomicBoolean(false);
    private Thread worker;

    public WorkflowSagaWorker(WorkflowContinuationQueue queue, OrderWorkflowService workflow) {
        this.queue = queue;
        this.workflow = workflow;
    }

    @PostConstruct
    public void start() {
        running.set(true);
        worker = new Thread(this::pump, "shoppingmall-workflow-saga-worker");
        worker.setDaemon(true);
        worker.start();
    }

    @PreDestroy
    public void stop() {
        running.set(false);
        queue.signalStop();
        if (worker != null) {
            worker.interrupt();
        }
    }

    private void pump() {
        while (running.get()) {
            String orderId;
            try {
                orderId = queue.take();
            } catch (InterruptedException ignored) {
                Thread.currentThread().interrupt();
                return;
            }
            if (orderId == null) {
                return;
            }
            try {
                OrderState state = workflow.continueWorkflow(orderId);
                System.err.printf(
                    "shoppingmall order: advanced order=%s status=%s%n",
                    orderId,
                    state.status());
            } catch (RuntimeException error) {
                System.err.printf(
                    "shoppingmall order: saga failed order=%s error=%s%n",
                    orderId,
                    error.getMessage());
            }
        }
    }
}
