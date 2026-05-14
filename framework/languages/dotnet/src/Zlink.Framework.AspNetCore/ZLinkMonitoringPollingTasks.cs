namespace Zlink.Framework.AspNetCore;

internal sealed class ZLinkMonitoringPollingTasks(int capacity)
{
    private readonly List<Task> _tasks = new(capacity);

    public void Add(Task task)
    {
        _tasks.Add(task);
    }

    public Task WaitAsync()
    {
        return _tasks.Count == 0
            ? Task.CompletedTask
            : Task.WhenAll(_tasks);
    }
}
