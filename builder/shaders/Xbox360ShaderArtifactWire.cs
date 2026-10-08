using System.Buffers.Binary;

namespace helengine.x360.builder;

/// <summary>Private X3SP v1 wire constants and checked little-endian writers.</summary>
internal static class Xbox360ShaderArtifactWire {
    public const int HeaderSize = 64;
    public const int DirectoryOffset = 64;
    public const int DirectoryEntrySize = 16;
    public const int SectionCount = 8;
    public const int FirstSectionOffset = HeaderSize + DirectoryEntrySize * SectionCount;
    public const int MaximumFileBytes = 1_048_576;
    public const int MaximumStringBytes = 4096;
    public const int MaximumStringPoolBytes = 262_144;
    public const uint Absent = uint.MaxValue;

    public static readonly int[] RecordSizes = [48, 160, 4, 36, 40, 24, 24, 1];

    public static int CheckedLength(int count, int recordSize) => checked(count * recordSize);
    public static int Align4(int value) => checked((value + 3) & ~3);

    public static void WriteUInt32(byte[] destination, int offset, uint value) =>
        BinaryPrimitives.WriteUInt32LittleEndian(destination.AsSpan(offset, sizeof(uint)), value);

    public static void WriteUInt32(byte[] destination, int offset, int value) {
        if (value < 0) {
            throw new InvalidDataException("A negative value cannot be represented in the X3SP v1 wire format.");
        }
        WriteUInt32(destination, offset, checked((uint)value));
    }

    public static void WriteRange(byte[] destination, int offset, int sectionOffset, int recordIndex, int count, int recordSize) {
        if (count == 0) {
            WriteUInt32(destination, offset, 0);
            WriteUInt32(destination, offset + 4, 0);
            return;
        }
        int absolute = checked(sectionOffset + CheckedLength(recordIndex, recordSize));
        WriteUInt32(destination, offset, absolute);
        WriteUInt32(destination, offset + 4, count);
    }

    public static void WriteDigest(byte[] destination, int offset, string hex, string field) {
        if (hex == null || hex.Length != 64) {
            throw new InvalidDataException($"{field} must contain exactly 64 hexadecimal characters.");
        }
        byte[] bytes;
        try {
            bytes = Convert.FromHexString(hex);
        } catch (FormatException exception) {
            throw new InvalidDataException($"{field} is not a valid hexadecimal SHA-256 digest.", exception);
        }
        bytes.CopyTo(destination, offset);
    }
}
