// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed partial class Spot
{
    private static void OnRoutedReplyCallback(int result, IntPtr parts,
        nuint partCount, IntPtr userData)
    {
        var handle = GCHandle.FromIntPtr(userData);
        var state =
            (SpotRequestCallbackState)handle.Target!;
        try
        {
            if (!state.TryStartCompletion())
                return;

            if (result != 0)
            {
                state.Invoke((RequestResult)result, Array.Empty<Message>());
                return;
            }

            var replyParts = Message.FromNativeVector(parts, partCount);
            parts = IntPtr.Zero;
            partCount = 0;
            state.Invoke(RequestResult.Ok, replyParts);
        }
        finally
        {
            if (parts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(parts, partCount);
            handle.Free();
        }
    }

    private sealed class SpotRequestCallbackState
    {
        private readonly Action<RequestResult, IReadOnlyList<Message>> _callback;
        private readonly RequestProgressPump.ProgressLease _progress;
        private int _completed;

        internal SpotRequestCallbackState(
            Action<RequestResult, IReadOnlyList<Message>> callback,
            RequestProgressPump.ProgressLease progress)
        {
            _callback = callback;
            _progress = progress;
        }

        internal bool TryStartCompletion()
        {
            if (Interlocked.Exchange(ref _completed, 1) != 0)
                return false;
            DisposeProgress();
            return true;
        }

        internal void DisposeProgress()
        {
            _progress.Dispose();
        }

        internal void Invoke(RequestResult result,
            IReadOnlyList<Message> payload)
        {
            try
            {
                _callback(result, payload);
            }
            catch (Exception ex)
            {
                CallbackExceptionHub.Report(ex);
            }
        }
    }
}