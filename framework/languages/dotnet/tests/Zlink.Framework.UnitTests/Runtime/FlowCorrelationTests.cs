using Systems.Zlink.Stream.Connector.Runtime.Protocol;

namespace Zlink.Framework.UnitTests;

public sealed class FlowCorrelationTests
{
    [Fact]
    public void UuidV7_generator_emits_the_frozen_canonical_format()
    {
        var value = ZlinkStreamFlowId.Create();

        Assert.Equal(36, value.Length);
        Assert.Equal(value.ToLowerInvariant(), value);
        Assert.Equal('7', value[14]);
        Assert.True(value[19] is '8' or '9' or 'a' or 'b');
        Assert.True(ZlinkStreamFlowId.IsValid(value));
    }

    [Fact]
    public async Task Awaited_work_keeps_flow_but_detached_work_loses_the_expired_lease()
    {
        Task<ZLinkFlowValue?> detached;
        string flowId;
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        using (ZLinkFlowContext.Enter(null, null, true, ZLinkFlowOrigin.Inbound))
        {
            flowId = Assert.IsType<ZLinkFlowValue>(ZLinkFlowContext.Current).FlowId;
            await Task.Yield();
            Assert.Equal(flowId, ZLinkFlowContext.Current?.FlowId);

            detached = Task.Run(async () =>
            {
                await release.Task;
                return ZLinkFlowContext.Current;
            });
        }

        release.SetResult();
        Assert.Null(await detached);
        var application = ZLinkFlowContext.CurrentOrCreate(ZLinkFlowOrigin.Application);
        Assert.NotEqual(flowId, application.FlowId);
        Assert.Equal(ZLinkFlowOrigin.Application, application.Origin);
    }
}
