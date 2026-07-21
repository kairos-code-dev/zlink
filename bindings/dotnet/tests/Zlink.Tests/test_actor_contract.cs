using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using Xunit;

namespace Systems.Zlink.Tests;

public sealed class test_actor_contract
{
    private static MethodInfo FindPublicInstanceMethod(Type type, string name)
    {
        return type.GetMethods()
            .Concat(type.GetInterfaces().SelectMany(current =>
                current.GetMethods()))
            .Single(method => method.Name == name);
    }

    [Fact]
    public void mesh_node_actor_send_and_request_surface_exists()
    {
        MethodInfo send = FindPublicInstanceMethod(
            typeof(IMeshNode),
            nameof(IMeshNode.SendToActor));
        Assert.Equal(typeof(SubmitResult), send.ReturnType);
        Assert.Equal(
            new[]
            {
                typeof(ActorRef),
                typeof(IReadOnlyList<Message>),
                typeof(SendFlags),
                typeof(ReadOnlyMemory<byte>)
            },
            send.GetParameters().Select(parameter => parameter.ParameterType));

        MethodInfo request = FindPublicInstanceMethod(
            typeof(IMeshNode),
            nameof(IMeshNode.RequestToActor));
        Assert.Equal(typeof(SubmitResult), request.ReturnType);
        Assert.Equal(typeof(MeshOperationId).MakeByRefType(),
            request.GetParameters()[2].ParameterType);
    }

    [Fact]
    public void actor_lifecycle_uses_pull_dispatch_payloads()
    {
        Assert.Equal(MeshRecordKind.SpotControl,
            Enum.Parse<MeshRecordKind>("SpotControl"));
        Assert.Equal(MeshOperationKind.ActorJoin,
            Enum.Parse<MeshOperationKind>("ActorJoin"));

        Assert.Equal(typeof(ActorControlRecord),
            typeof(MeshReceiveRecord)
                .GetProperty(nameof(MeshReceiveRecord.ActorControl))!
                .PropertyType);
        Assert.Equal(typeof(ActorJoinCompletion),
            typeof(MeshReceiveRecord)
                .GetProperty(nameof(MeshReceiveRecord.JoinCompletion))!
                .PropertyType);
    }

    [Fact]
    public void mesh_receive_record_projects_binding_generation()
    {
        Assert.Equal(typeof(ulong),
            typeof(MeshReceiveRecord)
                .GetProperty(nameof(MeshReceiveRecord.SourceBindingGeneration))!
                .PropertyType);
    }

}
