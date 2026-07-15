namespace Systems.Zlink.Stream.Connector.Contracts;

/// <summary>
///     Provides assertions that do not depend on connector state.
/// </summary>
public static class ZlinkStreamAssert
{
    /// <summary>
    ///     Throws with the required diagnostic message when the condition is false.
    /// </summary>
    public static void Ensure(bool condition, string message)
    {
        if (string.IsNullOrWhiteSpace(message))
            throw new ArgumentException("Assertion message is required.", nameof(message));
        if (!condition) throw new InvalidOperationException(message);
    }

    /// <summary>
    ///     Executes an action, requires it to fail, and optionally verifies its error kind.
    /// </summary>
    public static async ValueTask<ZlinkStreamError> ExpectFailureAsync(
        Func<CancellationToken, ValueTask> action,
        string? errorKind = null)
    {
        ArgumentNullException.ThrowIfNull(action);

        try
        {
            await action(CancellationToken.None).ConfigureAwait(false);
        }
        catch (Exception exception)
        {
            var error = Classify(exception);
            if (errorKind is not null
                && !string.Equals(error.Code.ToString(), errorKind, StringComparison.Ordinal))
                throw new InvalidOperationException(
                    $"Expected failure kind '{errorKind}', got '{error.Code}'.",
                    exception);
            return error;
        }

        throw new InvalidOperationException("Expected action to fail.");
    }

    /// <summary>
    ///     Executes an action and requires a timeout failure; other failures are propagated.
    /// </summary>
    public static async ValueTask ExpectTimeoutAsync(
        Func<CancellationToken, ValueTask> action)
    {
        ArgumentNullException.ThrowIfNull(action);

        try
        {
            await action(CancellationToken.None).ConfigureAwait(false);
        }
        catch (TimeoutException)
        {
            return;
        }
        catch (ZlinkStreamException exception) when (
            exception.Error.Code is ZlinkStreamErrorCode.RequestTimeout or ZlinkStreamErrorCode.ConnectTimeout)
        {
            return;
        }

        throw new InvalidOperationException("Expected action to time out.");
    }

    private static ZlinkStreamError Classify(Exception exception)
    {
        if (exception is ZlinkStreamException streamException) return streamException.Error;

        var code = exception switch
        {
            TimeoutException => ZlinkStreamErrorCode.RequestTimeout,
            ArgumentException => ZlinkStreamErrorCode.ValidationFailed,
            IOException => ZlinkStreamErrorCode.Disconnected,
            _ => ZlinkStreamErrorCode.RemoteError
        };
        return new ZlinkStreamError(code, exception.Message, exception);
    }
}
