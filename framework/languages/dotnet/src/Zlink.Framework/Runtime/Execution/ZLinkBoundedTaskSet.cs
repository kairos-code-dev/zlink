using System.Runtime.ExceptionServices;

namespace Zlink.Framework.Runtime;

internal sealed class ZLinkBoundedTaskSet(int maxConcurrency)
{
    private readonly List<Task> _activeTasks = [];
    private ExceptionDispatchInfo? _firstFailure;

    public async ValueTask AddAsync(Task task)
    {
        _activeTasks.Add(task);
        if (_activeTasks.Count >= maxConcurrency)
        {
            await ObserveOneAsync().ConfigureAwait(false);
        }
    }

    public async ValueTask DrainAsync()
    {
        while (_activeTasks.Count > 0)
        {
            await ObserveOneAsync().ConfigureAwait(false);
        }

        _firstFailure?.Throw();
    }

    private async ValueTask ObserveOneAsync()
    {
        var completed = await Task.WhenAny(_activeTasks).ConfigureAwait(false);
        _activeTasks.Remove(completed);
        try
        {
            await completed.ConfigureAwait(false);
        }
        catch (Exception ex) when (_firstFailure is null)
        {
            _firstFailure = ExceptionDispatchInfo.Capture(ex);
        }
        catch
        {
        }
    }
}
