using System.Collections.Concurrent;
using System.Linq.Expressions;
using System.Reflection;

namespace Zlink.Framework.Runtime.Handlers;

internal delegate object? ZLinkHandlerMethodInvoker(object target, object?[] arguments);

internal static class ZLinkHandlerMethodInvokerFactory
{
    private static readonly ConcurrentDictionary<MethodInfo, ZLinkHandlerMethodInvoker> Cache = new();

    public static ZLinkHandlerMethodInvoker Create(MethodInfo method)
    {
        ArgumentNullException.ThrowIfNull(method);
        return Cache.GetOrAdd(method, Compile);
    }

    private static ZLinkHandlerMethodInvoker Compile(MethodInfo method)
    {
        if (method.ContainsGenericParameters)
            throw new ZLinkConfigurationException(
                $"Handler method '{method.DeclaringType?.FullName}.{method.Name}' must not contain open generic parameters.");

        var parameters = method.GetParameters();
        foreach (var parameter in parameters)
            if (parameter.ParameterType.IsByRef)
                throw new ZLinkConfigurationException(
                    $"Handler method '{method.DeclaringType?.FullName}.{method.Name}' must not use ref, in, or out parameters.");

        var target = Expression.Parameter(typeof(object), "target");
        var arguments = Expression.Parameter(typeof(object?[]), "arguments");

        var convertedArguments = new Expression[parameters.Length];
        for (var i = 0; i < parameters.Length; i++)
        {
            var argument = Expression.ArrayIndex(arguments, Expression.Constant(i));
            convertedArguments[i] = Expression.Convert(argument, parameters[i].ParameterType);
        }

        var instance = method.IsStatic
            ? null
            : Expression.Convert(target, method.DeclaringType!);
        var call = Expression.Call(instance, method, convertedArguments);

        Expression body = method.ReturnType == typeof(void)
            ? Expression.Block(call, Expression.Constant(null, typeof(object)))
            : Expression.Convert(call, typeof(object));

        return Expression
            .Lambda<ZLinkHandlerMethodInvoker>(body, target, arguments)
            .Compile();
    }
}