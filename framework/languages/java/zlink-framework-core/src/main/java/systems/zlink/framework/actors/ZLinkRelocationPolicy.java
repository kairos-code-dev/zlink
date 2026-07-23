package systems.zlink.framework.actors;

/**
 * Selects how an object is reconstructed when ownership moves to another node.
 */
public sealed interface ZLinkRelocationPolicy<TInstance>
    permits ZLinkRelocationPolicy.Disabled,
            ZLinkRelocationPolicy.Recreate,
            ZLinkRelocationPolicy.Snapshot {

    record Disabled<T>() implements ZLinkRelocationPolicy<T> {
    }

    record Recreate<T>() implements ZLinkRelocationPolicy<T> {
    }

    record Snapshot<T>(Class<?> adapterClass)
        implements ZLinkRelocationPolicy<T> {
    }

    static <T> ZLinkRelocationPolicy<T> disabled() {
        return new Disabled<>();
    }

    static <T> ZLinkRelocationPolicy<T> recreate() {
        return new Recreate<>();
    }

    static <T> ZLinkRelocationPolicy<T> snapshot(Class<?> adapterClass) {
        return new Snapshot<>(adapterClass);
    }
}
