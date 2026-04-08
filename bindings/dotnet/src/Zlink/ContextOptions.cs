// SPDX-License-Identifier: MPL-2.0

namespace Zlink;

public sealed class ContextOptions
{
    private readonly Context _context;

    internal ContextOptions(Context context)
    {
        _context = context;
    }

    public int IoThreads
    {
        get => _context.GetOption(ContextOption.IoThreads);
        set => _context.SetOption(ContextOption.IoThreads, value);
    }

    public int MaxSockets
    {
        get => _context.GetOption(ContextOption.MaxSockets);
        set => _context.SetOption(ContextOption.MaxSockets, value);
    }

    public int SocketLimit => _context.GetOption(ContextOption.SocketLimit);

    public int ThreadPriority
    {
        get => _context.GetOption(ContextOption.ThreadPriority);
        set => _context.SetOption(ContextOption.ThreadPriority, value);
    }

    public int ThreadSchedulingPolicy
    {
        get => _context.GetOption(ContextOption.ThreadSchedPolicy);
        set => _context.SetOption(ContextOption.ThreadSchedPolicy, value);
    }

    public int MaxMessageSize
    {
        get => _context.GetOption(ContextOption.MaxMsgSz);
        set => _context.SetOption(ContextOption.MaxMsgSz, value);
    }

    public int MessageThreadSize => _context.GetOption(ContextOption.MsgTSize);

    public bool Blocky
    {
        get => _context.GetOption(ContextOption.Blocky) != 0;
        set => _context.SetOption(ContextOption.Blocky, value ? 1 : 0);
    }

    public void AddThreadAffinityCpu(int cpu)
    {
        _context.SetOption(ContextOption.ThreadAffinityCpuAdd, cpu);
    }

    public void RemoveThreadAffinityCpu(int cpu)
    {
        _context.SetOption(ContextOption.ThreadAffinityCpuRemove, cpu);
    }
}
