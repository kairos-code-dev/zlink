// SPDX-License-Identifier: MPL-2.0

using System.Collections;

namespace Systems.Zlink;

internal struct OperationMessageBuffer
{
    private Message? _singlePart;
    private List<Message>? _parts;

    internal int Count => _parts?.Count ?? (_singlePart == null ? 0 : 1);

    internal bool IsSingle => _singlePart != null;

    internal Message Single => _singlePart
                               ?? throw new ZlinkConfigException(
                                   ZlinkConfigException.ErrorCode.InvalidState);

    internal IReadOnlyList<Message> Parts =>
        _parts != null ? _parts : new SingleMessageReadOnlyList(Single);

    internal IReadOnlyList<Message> PartsOrEmpty =>
        Count == 0 ? Array.Empty<Message>() : Parts;

    internal void Add(Message message)
    {
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        if (_parts != null)
        {
            _parts.Add(message);
            return;
        }

        if (_singlePart == null)
        {
            _singlePart = message;
            return;
        }

        _parts = new List<Message> { _singlePart, message };
        _singlePart = null;
    }

    internal void EnsureNotEmpty()
    {
        if (Count == 0)
            throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidArgument);
    }

    private sealed class SingleMessageReadOnlyList : IReadOnlyList<Message>
    {
        private readonly Message _message;

        internal SingleMessageReadOnlyList(Message message)
        {
            _message = message;
        }

        public int Count => 1;

        public Message this[int index]
        {
            get
            {
                if (index != 0)
                    throw new ArgumentOutOfRangeException(nameof(index));
                return _message;
            }
        }

        public IEnumerator<Message> GetEnumerator()
        {
            yield return _message;
        }

        IEnumerator IEnumerable
            .GetEnumerator()
        {
            return GetEnumerator();
        }
    }
}