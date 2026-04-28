using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Text;
using Systems.Zlink.Stream.Connector.Abstractions;
using Systems.Zlink.Stream.Connector.Builders;
using Systems.Zlink.Stream.Connector.Codecs;
using Systems.Zlink.Stream.Connector.Compression;
using Systems.Zlink.Stream.Connector.Connector;
using Systems.Zlink.Stream.Connector.Framing;
using Systems.Zlink.Stream.Connector.Headers;
using Systems.Zlink.Stream.Connector.Metadata;
using Systems.Zlink.Stream.Connector.Options;
using Zlink;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.AspNetCore.Builder;

static class SampleProcess
{
    public static void ExitImmediately(int exitCode)
    {
        if (OperatingSystem.IsWindows())
        {
            Environment.Exit(exitCode);
        }

        Exit(exitCode);
    }

    [DllImport("libc", EntryPoint = "_exit")]
    private static extern void Exit(int status);
}
