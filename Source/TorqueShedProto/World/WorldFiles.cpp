#include "WorldFiles.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace TorqueWorld
{
namespace Files
{
namespace fs = std::filesystem;

// Paths are UTF-8 here. Windows' own are UTF-16, so they are turned to and fro there by hand (the
// standard library's ways of saying "this is UTF-8" changed between C++17 and C++20).
#ifdef _WIN32
static std::wstring WideOf(const std::string& Path)
{
	std::wstring Wide;
	for (size_t I = 0; I < Path.size();)
	{
		const unsigned char C = static_cast<unsigned char>(Path[I]);
		const int More = C < 0x80 ? 0 : (C < 0xe0 ? 1 : (C < 0xf0 ? 2 : 3));
		unsigned long Code = More == 0 ? C : (C & (0x3fu >> More));
		for (int K = 1; K <= More && I + size_t(K) < Path.size(); ++K) Code = (Code << 6) | (static_cast<unsigned char>(Path[I + size_t(K)]) & 0x3fu);
		I += size_t(More) + 1;
		if (Code >= 0x10000)
		{
			Code -= 0x10000;
			Wide.push_back(static_cast<wchar_t>(0xd800 + (Code >> 10)));
			Wide.push_back(static_cast<wchar_t>(0xdc00 + (Code & 0x3ff)));
		}
		else Wide.push_back(static_cast<wchar_t>(Code));
	}
	return Wide;
}

static std::string NarrowOf(const std::wstring& Wide)
{
	std::string Out;
	for (size_t I = 0; I < Wide.size(); ++I)
	{
		unsigned long Code = static_cast<unsigned long>(Wide[I]) & 0xffffu;
		if (Code >= 0xd800 && Code < 0xdc00 && I + 1 < Wide.size())
			Code = 0x10000 + ((Code - 0xd800) << 10) + ((static_cast<unsigned long>(Wide[++I]) & 0xffffu) - 0xdc00);
		if (Code < 0x80) Out.push_back(static_cast<char>(Code));
		else if (Code < 0x800)
		{
			Out.push_back(static_cast<char>(0xc0 | (Code >> 6)));
			Out.push_back(static_cast<char>(0x80 | (Code & 0x3f)));
		}
		else if (Code < 0x10000)
		{
			Out.push_back(static_cast<char>(0xe0 | (Code >> 12)));
			Out.push_back(static_cast<char>(0x80 | ((Code >> 6) & 0x3f)));
			Out.push_back(static_cast<char>(0x80 | (Code & 0x3f)));
		}
		else
		{
			Out.push_back(static_cast<char>(0xf0 | (Code >> 18)));
			Out.push_back(static_cast<char>(0x80 | ((Code >> 12) & 0x3f)));
			Out.push_back(static_cast<char>(0x80 | ((Code >> 6) & 0x3f)));
			Out.push_back(static_cast<char>(0x80 | (Code & 0x3f)));
		}
	}
	return Out;
}

static fs::path PathOf(const std::string& Path) { return fs::path(WideOf(Path)); }
static std::string NameOf(const fs::path& P) { return NarrowOf(P.wstring()); }
#else
static fs::path PathOf(const std::string& Path) { return fs::path(Path); }
static std::string NameOf(const fs::path& P) { return P.string(); }
#endif

std::string Join(const std::string& A, const std::string& B)
{
	if (A.empty()) return B;
	if (B.empty()) return A;
	return (A.back() == '/' || A.back() == '\\') ? A + B : A + "/" + B;
}

bool Exists(const std::string& Path)
{
	std::error_code Ec;
	return fs::is_regular_file(PathOf(Path), Ec);
}

bool Read(const std::string& Path, std::string& Out)
{
	std::ifstream F(PathOf(Path), std::ios::binary);
	if (!F) return false;
	F.seekg(0, std::ios::end);
	const std::streamoff N = F.tellg();
	if (N < 0) return false;
	F.seekg(0, std::ios::beg);
	Out.resize(size_t(N));
	if (N > 0) F.read(&Out[0], N);
	return bool(F);
}

bool Write(const std::string& Path, const std::string& Bytes)
{
	std::error_code Ec;
	const fs::path P = PathOf(Path);
	if (P.has_parent_path()) fs::create_directories(P.parent_path(), Ec);
	std::ofstream F(P, std::ios::binary | std::ios::trunc);
	if (!F) return false;
	F.write(Bytes.data(), std::streamsize(Bytes.size()));
	return bool(F);
}

bool Remove(const std::string& Path)
{
	std::error_code Ec;
	return fs::remove(PathOf(Path), Ec);
}

long long ModifiedUnix(const std::string& Path)
{
	std::error_code Ec;
	const fs::file_time_type T = fs::last_write_time(PathOf(Path), Ec);
	if (Ec) return -1;
	// (the file clock's own zero differs by platform: measured against now on both clocks)
	const auto Ago = std::chrono::duration_cast<std::chrono::seconds>(fs::file_time_type::clock::now() - T).count();
	const auto Now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	return static_cast<long long>(Now - Ago);
}

std::vector<std::string> List(const std::string& Dir, bool Folders)
{
	std::vector<std::string> Out;
	std::error_code Ec;
	for (fs::directory_iterator It(PathOf(Dir), Ec), End; !Ec && It != End; It.increment(Ec))
	{
		std::error_code Ec2;
		if (Folders ? It->is_directory(Ec2) : It->is_regular_file(Ec2)) Out.push_back(NameOf(It->path().filename()));
	}
	std::sort(Out.begin(), Out.end());
	return Out;
}
} // namespace Files
} // namespace TorqueWorld
