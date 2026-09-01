namespace KeyStats.Core;

public sealed class KeyboardCounter
{
    private readonly long[] _counts = new long[KeyCatalog.Count];
    private readonly HashSet<PressedKey> _pressed = [];
    private readonly object _pressedGate = new();
    private long _totalCount;
    private int _isPaused;

    public bool IsPaused => Volatile.Read(ref _isPaused) != 0;

    public bool Process(nint deviceHandle, RawKeyEvent keyEvent)
        => ProcessDetailed(deviceHandle, keyEvent).Recognized;

    public KeyProcessResult ProcessDetailed(nint deviceHandle, RawKeyEvent keyEvent)
    {
        var normalized = KeyNormalizer.Normalize(keyEvent);
        if (normalized is not { } keyId)
        {
            return KeyProcessResult.Unrecognized;
        }

        return ProcessButton(deviceHandle, keyId, keyEvent.IsBreak);
    }

    public KeyProcessResult ProcessButton(nint deviceHandle, KeyId keyId, bool isBreak)
    {
        if ((uint)keyId >= (uint)KeyCatalog.Count)
        {
            return KeyProcessResult.Unrecognized;
        }

        // Tab 在部分 Windows 10 环境的 Alt+Tab 系统切换中不会产生 Raw Input，
        // 因而需要与补偿采样共用一个全局身份，避免正常消息和补偿消息重复计数。
        var trackingDeviceHandle = keyId == KeyId.Tab ? 0 : deviceHandle;
        var pressedKey = new PressedKey(trackingDeviceHandle, keyId);
        if (isBreak)
        {
            lock (_pressedGate)
            {
                _pressed.Remove(pressedKey);
            }

            return KeyProcessResult.RecognizedOnly(keyId);
        }

        bool isNewPress;
        lock (_pressedGate)
        {
            isNewPress = _pressed.Add(pressedKey);
        }

        if (isNewPress && !IsPaused)
        {
            Interlocked.Increment(ref _counts[(int)keyId]);
            Interlocked.Increment(ref _totalCount);
            return KeyProcessResult.CountedPress(keyId);
        }

        return KeyProcessResult.RecognizedOnly(keyId);
    }

    public void SetPaused(bool paused) => Interlocked.Exchange(ref _isPaused, paused ? 1 : 0);

    public void ClearCounts(bool resetPressedState = false)
    {
        foreach (ref var count in _counts.AsSpan())
        {
            Interlocked.Exchange(ref count, 0);
        }

        Interlocked.Exchange(ref _totalCount, 0);

        if (resetPressedState)
        {
            lock (_pressedGate)
            {
                _pressed.Clear();
            }
        }
    }

    public void RemoveDevice(nint deviceHandle)
    {
        lock (_pressedGate)
        {
            _pressed.RemoveWhere(key => key.DeviceHandle == deviceHandle);
        }
    }

    public void ReleaseKey(KeyId keyId)
    {
        lock (_pressedGate)
        {
            _pressed.RemoveWhere(key => key.KeyId == keyId);
        }
    }

    public KeyboardSnapshot GetSnapshot()
    {
        var counts = new long[_counts.Length];
        for (var index = 0; index < _counts.Length; index++)
        {
            counts[index] = Interlocked.Read(ref _counts[index]);
        }

        HashSet<KeyId> pressedKeys;
        lock (_pressedGate)
        {
            pressedKeys = _pressed.Select(key => key.KeyId).ToHashSet();
        }

        return new KeyboardSnapshot(
            Interlocked.Read(ref _totalCount),
            counts,
            pressedKeys,
            IsPaused);
    }

    private readonly record struct PressedKey(nint DeviceHandle, KeyId KeyId);
}
