// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Concurrent;
using System.Threading;
using System.Threading.Tasks;

namespace Zlink;

internal static class CallbackDelivery
{
    private static readonly ManagedCallbackDispatcher Dispatcher = new();

    internal static void Post(SynchronizationContext? context, Action action)
    {
        if (action == null)
            throw new ArgumentNullException(nameof(action));

        if (context != null)
        {
            try
            {
                context.Post(static state =>
                {
                    ExecuteSafely((Action)state!);
                }, action);
                return;
            }
            catch
            {
                // Fall back to the managed dispatcher if the captured context
                // is unavailable or no longer usable.
            }
        }

        Dispatcher.Post(action);
    }

    internal static TResult Invoke<TResult>(SynchronizationContext? context,
        Func<TResult> action)
    {
        if (action == null)
            throw new ArgumentNullException(nameof(action));

        if (context != null)
        {
            if (ReferenceEquals(SynchronizationContext.Current, context))
                return action();

            var pending = new PendingInvocation<TResult>(action);
            try
            {
                context.Post(static state =>
                {
                    ((PendingInvocation<TResult>)state!).Run();
                }, pending);
                return pending.Completion.Task.GetAwaiter().GetResult();
            }
            catch
            {
                // Fall back to the managed dispatcher if the captured context
                // cannot accept work.
            }
        }

        return Dispatcher.Invoke(action);
    }

    private static void ExecuteSafely(Action action)
    {
        try
        {
            action();
        }
        catch (Exception ex)
        {
            Runtime.ReportUnhandledCallbackException(ex);
        }
    }

    private sealed class PendingInvocation<TResult>
    {
        private readonly Func<TResult> _action;
        internal readonly TaskCompletionSource<TResult> Completion =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        internal PendingInvocation(Func<TResult> action)
        {
            _action = action;
        }

        internal void Run()
        {
            try
            {
                Completion.SetResult(_action());
            }
            catch (Exception ex)
            {
                Completion.SetException(ex);
            }
        }
    }

    private sealed class ManagedCallbackDispatcher
    {
        private readonly BlockingCollection<Action> _queue = new();
        private readonly Thread _thread;
        private readonly ManualResetEventSlim _started = new(false);
        private volatile int _threadId;

        internal ManagedCallbackDispatcher()
        {
            _thread = new Thread(Run)
            {
                IsBackground = true,
                Name = "Zlink.CallbackDispatcher"
            };
            _thread.Start();
            _started.Wait();
        }

        internal void Post(Action action)
        {
            _queue.Add(() => ExecuteSafely(action));
        }

        internal TResult Invoke<TResult>(Func<TResult> action)
        {
            if (IsCurrentThread)
                return action();

            var pending = new PendingInvocation<TResult>(action);
            _queue.Add(pending.Run);
            return pending.Completion.Task.GetAwaiter().GetResult();
        }

        private bool IsCurrentThread =>
            Environment.CurrentManagedThreadId == _threadId;

        private void Run()
        {
            _threadId = Environment.CurrentManagedThreadId;
            _started.Set();

            foreach (Action action in _queue.GetConsumingEnumerable())
            {
                action();
            }
        }
    }
}
