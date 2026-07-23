package systems.zlink.framework.locations;

public sealed interface ZLinkAggregatePrepareResult
    permits ZLinkAggregatePrepared, ZLinkAggregateAlreadyPrepared,
        ZLinkAggregateConflict, ZLinkAggregateStale,
        ZLinkAggregateGenerationExhausted {
}
