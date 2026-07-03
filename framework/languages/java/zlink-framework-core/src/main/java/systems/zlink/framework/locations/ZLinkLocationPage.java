package systems.zlink.framework.locations;

import java.util.List;

public record ZLinkLocationPage<T>(
    List<T> items,
    String continuationToken) {
}
