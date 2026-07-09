using System.Collections.Concurrent;
using System.Linq.Expressions;
using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Handlers;

internal sealed class ZLinkHandlerActivator
{
    private static readonly ConcurrentDictionary<Type, Func<object>?> PublicParameterlessFactories = new();

    public object Create(IServiceProvider services, Type handlerType)
    {
        var handler = services.GetService(handlerType);
        if (handler is not null) return handler;

        var factory = PublicParameterlessFactories.GetOrAdd(handlerType, CreatePublicParameterlessFactory);
        if (factory is not null) return factory();

        throw new InvalidOperationException(
            $"Handler type '{handlerType}' is not registered in dependency injection and does not expose a public parameterless constructor.");
    }

    private static Func<object>? CreatePublicParameterlessFactory(Type handlerType)
    {
        var constructor = handlerType.GetConstructor(Type.EmptyTypes);
        if (constructor is null) return null;

        var create = Expression.New(constructor);
        var convert = Expression.Convert(create, typeof(object));
        return Expression.Lambda<Func<object>>(convert).Compile();
    }
}
