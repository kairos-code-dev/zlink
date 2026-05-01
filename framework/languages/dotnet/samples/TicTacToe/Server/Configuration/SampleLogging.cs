using System.Collections.Concurrent;
using Microsoft.Extensions.Logging;

namespace TicTacToe.Server;

internal static class SampleLogging
{
    public static void Configure(ILoggingBuilder logging, SampleSettings settings, string role)
    {
        logging.ClearProviders();
        logging.SetMinimumLevel(LogLevel.Information);
        logging.AddSimpleConsole(options =>
        {
            options.SingleLine = true;
            options.TimestampFormat = "HH:mm:ss.fff ";
        });
        logging.AddProvider(new SampleFileLoggerProvider(
            Path.Combine(settings.LogDirectory, $"{role}.log")));
    }

}

internal sealed class SampleFileLoggerProvider : ILoggerProvider
{
    private readonly StreamWriter _writer;
    private readonly object _gate = new();
    private readonly ConcurrentDictionary<string, SampleFileLogger> _loggers = new();

    public SampleFileLoggerProvider(string path)
    {
        var directory = Path.GetDirectoryName(path);
        if (!string.IsNullOrWhiteSpace(directory))
        {
            Directory.CreateDirectory(directory);
        }

        _writer = new StreamWriter(new FileStream(
            path,
            FileMode.Append,
            FileAccess.Write,
            FileShare.ReadWrite))
        {
            AutoFlush = true,
        };
    }

    public ILogger CreateLogger(string categoryName)
    {
        return _loggers.GetOrAdd(categoryName, static (category, provider) =>
            new SampleFileLogger(category, provider),
            this);
    }

    public void Dispose()
    {
        lock (_gate)
        {
            _writer.Dispose();
        }
    }

    private void Write(
        string category,
        LogLevel level,
        EventId eventId,
        string message,
        Exception? exception)
    {
        lock (_gate)
        {
            _writer.Write(DateTimeOffset.Now.ToString("yyyy-MM-dd HH:mm:ss.fff zzz"));
            _writer.Write(" [");
            _writer.Write(level);
            _writer.Write("] ");
            _writer.Write(category);
            if (eventId.Id != 0)
            {
                _writer.Write(" (");
                _writer.Write(eventId.Id);
                _writer.Write(')');
            }

            _writer.Write(": ");
            _writer.WriteLine(message);
            if (exception is not null)
            {
                _writer.WriteLine(exception);
            }
        }
    }

    private sealed class SampleFileLogger(
        string category,
        SampleFileLoggerProvider provider) : ILogger
    {
        public IDisposable? BeginScope<TState>(TState state)
            where TState : notnull
        {
            _ = state;
            return null;
        }

        public bool IsEnabled(LogLevel logLevel)
        {
            return logLevel >= LogLevel.Information;
        }

        public void Log<TState>(
            LogLevel logLevel,
            EventId eventId,
            TState state,
            Exception? exception,
            Func<TState, Exception?, string> formatter)
        {
            if (!IsEnabled(logLevel))
            {
                return;
            }

            provider.Write(category, logLevel, eventId, formatter(state, exception), exception);
        }
    }
}
