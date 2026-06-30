namespace Zlink.Framework.Contracts.Channels;

/// <summary>
///     실행 중인 channel 의 옵션을 channel 이름으로 얻는 접근자. 빌더의 <c>Add&lt;Kind&gt;Channel(name)</c>
///     표면을 미러링한다: <c>runtime.&lt;Kind&gt;Channel(name).Configure&lt;Aspect&gt;()</c> 가
///     <c>builder.Add&lt;Kind&gt;Channel(name).Configure&lt;Aspect&gt;()</c> 와 같은 옵션 인터페이스를 돌려준다.
///     단 돌려받은 config 객체는 live serving socket backing 이라, runtime-mutable 속성(<c>Weight</c>)
///     set 은 live socket 에 즉시 반영되고 startup 전용 속성 set 은 명확한 오류로 거부된다.
///     drain 은 <c>ClientServerChannel(name).ConfigureServerSocket().Weight = 0</c>.
/// </summary>
public interface IZLinkChannelRuntimeOptions
{
    IZLinkClientServerChannelOptions ClientServerChannel(string channelName);

    IZLinkRouteMeshChannelOptions RouteMeshChannel(string channelName);
}