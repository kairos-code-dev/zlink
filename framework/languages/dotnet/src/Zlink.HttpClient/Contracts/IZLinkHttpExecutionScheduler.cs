namespace Zlink.HttpClient;

/// <summary>
///     Provides the server execution boundary used by HTTP <c>Yield</c> calls.
///     Standalone clients do not install a scheduler.
/// </summary>
internal interface IZLinkHttpExecutionScheduler
{
    ValueTask<TResult> YieldAsync<TResult>(
        Func<CancellationToken, ValueTask<TResult>> operation,
        CancellationToken cancellationToken = default);
}
