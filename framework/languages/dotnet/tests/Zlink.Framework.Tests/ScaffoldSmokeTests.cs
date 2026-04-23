using System.Reflection;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.Tests;

public sealed class ScaffoldSmokeTests
{
    private static readonly HashSet<Type> AllowedBackendTypes =
    [
        typeof(global::Zlink.Message),
        typeof(global::Zlink.RoutingId),
        typeof(global::Zlink.SendFlags),
    ];

    [Fact]
    public void FrameworkRoot_IsDiscoverable_FromTestRuntime()
    {
        var frameworkRoot = Common.FrameworkTestEnvironment.GetFrameworkRoot();
        var solutionPath = Path.Combine(frameworkRoot, "Zlink.Framework.sln");

        Assert.True(File.Exists(solutionPath), $"Missing framework solution at '{solutionPath}'.");
    }

    [Fact]
    public void PublicSurface_DoesNotExpose_BackendConcreteTypes()
    {
        AssertBackendLeakageFree(typeof(ZLinkFrameworkAssemblyMarker).Assembly);
        AssertBackendLeakageFree(typeof(ZLinkAspNetCoreAssemblyMarker).Assembly);
    }

    private static void AssertBackendLeakageFree(Assembly assembly)
    {
        foreach (var type in assembly.GetExportedTypes())
        {
            AssertPublicBoundary(type);

            foreach (var member in type.GetMembers(BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public))
            {
                switch (member)
                {
                    case MethodInfo method:
                        AssertTypeAllowed(method.ReturnType);

                        foreach (var parameter in method.GetParameters())
                        {
                            AssertTypeAllowed(parameter.ParameterType);
                        }

                        break;
                    case PropertyInfo property:
                        AssertTypeAllowed(property.PropertyType);
                        break;
                    case FieldInfo field:
                        AssertTypeAllowed(field.FieldType);
                        break;
                    case EventInfo @event:
                        AssertTypeAllowed(@event.EventHandlerType);
                        break;
                }
            }
        }
    }

    private static void AssertPublicBoundary(Type type)
    {
        AssertTypeAllowed(type);

        if (type.BaseType is not null)
        {
            AssertTypeAllowed(type.BaseType);
        }

        foreach (var implementedInterface in type.GetInterfaces())
        {
            AssertTypeAllowed(implementedInterface);
        }
    }

    private static void AssertTypeAllowed(Type? type)
    {
        if (type is null)
        {
            return;
        }

        if (type.IsGenericType)
        {
            foreach (var genericArgument in type.GetGenericArguments())
            {
                AssertTypeAllowed(genericArgument);
            }
        }

        if (type.HasElementType)
        {
            AssertTypeAllowed(type.GetElementType());
            return;
        }

        if (type.Assembly != typeof(global::Zlink.Context).Assembly)
        {
            return;
        }

        Assert.Contains(type, AllowedBackendTypes);
    }
}
