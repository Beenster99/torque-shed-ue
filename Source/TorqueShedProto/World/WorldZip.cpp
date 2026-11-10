#include "WorldZip.h"

#include "WorldInflate.h"

namespace TorqueWorld
{
namespace
{
uint32_t U16(const std::string& Z, size_t At) { return uint32_t(uint8_t(Z[At])) | (uint32_t(uint8_t(Z[At + 1])) << 8); }
uint32_t U32(const std::string& Z, size_t At) { return U16(Z, At) | (U16(Z, At + 2) << 16); }
} // namespace

uint32_t Crc32(const uint8_t* Data, size_t Size)
{
	static uint32_t Table[256];
	static bool Made = false;
	if (!Made)
	{
		for (uint32_t I = 0; I < 256; ++I)
		{
			uint32_t C = I;
			for (int K = 0; K < 8; ++K) C = (C & 1) ? 0xedb88320u ^ (C >> 1) : C >> 1;
			Table[I] = C;
		}
		Made = true;
	}
	uint32_t C = 0xffffffffu;
	for (size_t I = 0; I < Size; ++I) C = Table[(C ^ Data[I]) & 0xff] ^ (C >> 8);
	return C ^ 0xffffffffu;
}

bool ZipList(const std::string& Zip, std::vector<ZipEntry>& Out)
{
	Out.clear();
	if (Zip.size() < 22) return false;
	// The end record is at the back, behind at most a 64 KB comment.
	size_t End = std::string::npos;
	const size_t Lowest = Zip.size() > 22 + 65535 ? Zip.size() - 22 - 65535 : 0;
	for (size_t At = Zip.size() - 22;; --At)
	{
		if (U32(Zip, At) == 0x06054b50u)
		{
			End = At;
			break;
		}
		if (At == Lowest) break;
	}
	if (End == std::string::npos) return false;
	const uint32_t Count = U16(Zip, End + 10);
	size_t At = U32(Zip, End + 16);
	for (uint32_t I = 0; I < Count; ++I)
	{
		if (At + 46 > Zip.size() || U32(Zip, At) != 0x02014b50u) return false;
		ZipEntry E;
		const uint32_t Flags = U16(Zip, At + 8);
		E.Method = U16(Zip, At + 10);
		E.Crc = U32(Zip, At + 16);
		E.Packed = U32(Zip, At + 20);
		E.Size = U32(Zip, At + 24);
		const size_t NameLen = U16(Zip, At + 28), ExtraLen = U16(Zip, At + 30), CommentLen = U16(Zip, At + 32);
		E.Offset = U32(Zip, At + 42);
		if (At + 46 + NameLen > Zip.size() || (Flags & 1) != 0) return false;   // (encrypted: not the store's)
		E.Name = Zip.substr(At + 46, NameLen);
		Out.push_back(E);
		At += 46 + NameLen + ExtraLen + CommentLen;
	}
	return true;
}

bool ZipRead(const std::string& Zip, const ZipEntry& Entry, std::string& Out)
{
	Out.clear();
	const size_t At = Entry.Offset;
	if (At + 30 > Zip.size() || U32(Zip, At) != 0x04034b50u) return false;
	const size_t Data = At + 30 + U16(Zip, At + 26) + U16(Zip, At + 28);
	if (Data + Entry.Packed > Zip.size()) return false;
	if (Entry.Method == 0)
	{
		if (Entry.Packed != Entry.Size) return false;
		Out.assign(Zip, Data, Entry.Size);
	}
	else if (Entry.Method == 8)
	{
		if (!Inflate(reinterpret_cast<const uint8_t*>(Zip.data()) + Data, Entry.Packed, Out, Entry.Size) || Out.size() != Entry.Size) return false;
	}
	else return false;
	return Crc32(reinterpret_cast<const uint8_t*>(Out.data()), Out.size()) == Entry.Crc;
}
} // namespace TorqueWorld
