namespace Zlink.Framework.Locations.Redis;

internal static partial class ZLinkRedisAuthorityScripts
{
    internal const string ReadCapacityProjection = """
        return {
            redis.call('HGET', KEYS[1], ARGV[1]) or '0',
            redis.call('HGET', KEYS[2], ARGV[1]) or '0',
            redis.call('HGET', KEYS[3], ARGV[2]) or '0',
            redis.call('HGET', KEYS[4], ARGV[2]) or '0'
        }
        """;
}
