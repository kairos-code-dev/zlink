using RegistrationCodec.Server.InvalidDuplicate.Handlers;
using RegistrationCodec.Server.InvalidDuplicate;
namespace RegistrationCodec.Server.InvalidDuplicate.Infrastructure;

internal sealed class SingletonProbe
{
    public string Id { get; } = Guid.NewGuid().ToString("N");
}

internal sealed class ScopedProbe : IDisposable
{
    private static int _disposedCount;

    public string Id { get; } = Guid.NewGuid().ToString("N");

    public static int DisposedCount => Volatile.Read(ref _disposedCount);

    public void Dispose()
    {
        Interlocked.Increment(ref _disposedCount);
    }
}
