// The world store's files on disk (Ride UK stage 1): the one place the plain C++ world code touches
// the file system, so the game can put its own behind it if it ever needs to. Paths use '/'.
#pragma once

#include <string>
#include <vector>

namespace TorqueWorld
{
namespace Files
{
std::string Join(const std::string& A, const std::string& B);
bool Exists(const std::string& Path);
// The whole file's bytes; false when it can't be read.
bool Read(const std::string& Path, std::string& Out);
// Writes the file (making its folders); false when it can't.
bool Write(const std::string& Path, const std::string& Bytes);
bool Remove(const std::string& Path);
// When the file was last written (unix seconds), or -1.
long long ModifiedUnix(const std::string& Path);
// The names of the folders (or files) straight under a folder, sorted.
std::vector<std::string> List(const std::string& Dir, bool Folders);
} // namespace Files
} // namespace TorqueWorld
