package systems.zlink.framework.runtime.service;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.Test;

final class ZLinkRelocationSchedulerTest {
    @Test
    void startsReadyUnitsWithoutWaitingForRegistrationOrder()
        throws Exception {
        ZLinkRelocationScheduler scheduler =
            new ZLinkRelocationScheduler(2, 1024);
        CompletableFuture<Void> firstReady = new CompletableFuture<>();
        List<String> started = new ArrayList<>();

        var run = scheduler.schedule(List.of(
            new ZLinkRelocationScheduler.Unit(
                "first",
                1,
                firstReady,
                () -> {
                    started.add("first");
                    return CompletableFuture.completedFuture(null);
                }),
            new ZLinkRelocationScheduler.Unit(
                "second",
                1,
                CompletableFuture.completedFuture(null),
                () -> {
                    started.add("second");
                    return CompletableFuture.completedFuture(null);
                })));

        assertEquals(List.of("second"), started);
        assertFalse(run.completion().toCompletableFuture().isDone());
        firstReady.complete(null);
        run.completion().toCompletableFuture().get(1, TimeUnit.SECONDS);
        assertEquals(List.of("second", "first"), started);
    }

    @Test
    void enforces64UnitAnd256MiBDefaultBudgets() throws Exception {
        ZLinkRelocationScheduler scheduler =
            new ZLinkRelocationScheduler();
        List<CompletableFuture<Void>> actions = new ArrayList<>();
        List<ZLinkRelocationScheduler.Unit> units = new ArrayList<>();
        long fourMiB = 4L * 1024 * 1024;
        for (int index = 0; index < 65; index++) {
            CompletableFuture<Void> action = new CompletableFuture<>();
            actions.add(action);
            units.add(new ZLinkRelocationScheduler.Unit(
                "unit-" + index,
                fourMiB,
                CompletableFuture.completedFuture(null),
                () -> action));
        }

        var run = scheduler.schedule(units);
        assertEquals(64, run.snapshot().activeUnits());
        assertEquals(
            ZLinkRelocationScheduler.DEFAULT_MAX_IN_FLIGHT_BYTES,
            run.snapshot().inFlightBytes());
        actions.getFirst().complete(null);
        assertEquals(64, run.snapshot().activeUnits());
        assertEquals(1, run.snapshot().completedUnits());

        actions.subList(1, actions.size()).forEach(
            action -> action.complete(null));
        run.completion().toCompletableFuture().get(1, TimeUnit.SECONDS);
        assertTrue(run.snapshot().terminal());
        assertEquals(65, run.snapshot().completedUnits());
    }

    @Test
    void oversizedUnitRunsAloneAfterCurrentInflightDrains()
        throws Exception {
        ZLinkRelocationScheduler scheduler =
            new ZLinkRelocationScheduler(4, 100);
        CompletableFuture<Void> regular = new CompletableFuture<>();
        CompletableFuture<Void> oversized = new CompletableFuture<>();
        var run = scheduler.schedule(List.of(
            new ZLinkRelocationScheduler.Unit(
                "regular",
                80,
                CompletableFuture.completedFuture(null),
                () -> regular),
            new ZLinkRelocationScheduler.Unit(
                "oversized",
                150,
                CompletableFuture.completedFuture(null),
                () -> oversized)));

        assertEquals(1, run.snapshot().activeUnits());
        assertEquals(80, run.snapshot().inFlightBytes());
        regular.complete(null);
        assertEquals(1, run.snapshot().activeUnits());
        assertEquals(150, run.snapshot().inFlightBytes());
        oversized.complete(null);
        run.completion().toCompletableFuture().get(1, TimeUnit.SECONDS);
        assertEquals(2, run.snapshot().completedUnits());
    }
}
