// 32-bit glibc readdir/stat must accept the host filesystem's 64-bit inode
// numbers. Otherwise directory enumeration can fail with EOVERFLOW even when
// every test file is small. Keep this before any libc headers are included.
#if defined(__linux__) && !defined(__ANDROID__) && !defined(_FILE_OFFSET_BITS)
#define _FILE_OFFSET_BITS 64
#endif

#include "ra_file_util.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <chrono>
#include <filesystem>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef __ANDROID__
#include "xm8jni.h"
#endif

namespace Xm8Ra {
namespace {

void SetError(std::string *error, const char *message)
{
	if (error != nullptr) {
		*error = message;
	}
}

#ifdef _WIN32
namespace fs = std::filesystem;

bool Utf8Path(const std::string& path, fs::path *output)
{
	if (output == nullptr || path.empty() ||
		path.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
		return false;
	}
	const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
		path.data(), static_cast<int>(path.size()), nullptr, 0);
	if (length <= 0) {
		return false;
	}
	std::wstring wide(static_cast<size_t>(length), L'\0');
	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(),
		static_cast<int>(path.size()), &wide[0], length) != length) {
		return false;
	}
	if (wide.size() >= MAX_PATH) {
		for (wchar_t& value : wide) {
			if (value == L'/') value = L'\\';
		}
		if (wide.compare(0, 4, L"\\\\?\\") == 0) {
			// Already in extended-length form.
		}
		else if (wide.size() >= 3 && wide[1] == L':' && wide[2] == L'\\') {
			wide.insert(0, L"\\\\?\\");
		}
		else if (wide.compare(0, 2, L"\\\\") == 0) {
			wide = L"\\\\?\\UNC\\" + wide.substr(2);
		}
		else {
			const DWORD required = GetFullPathNameW(wide.c_str(), 0, nullptr,
				nullptr);
			if (required == 0) return false;
			std::wstring absolute(static_cast<size_t>(required), L'\0');
			const DWORD written = GetFullPathNameW(wide.c_str(), required,
				&absolute[0], nullptr);
			if (written == 0 || written >= required) return false;
			absolute.resize(written);
			wide = L"\\\\?\\" + absolute;
		}
	}
	*output = fs::path(wide);
	return true;
}

RaFileKind KindFromStatus(const fs::file_status& status)
{
	if (!fs::exists(status)) return RaFileKind::Missing;
	if (fs::is_regular_file(status)) return RaFileKind::Regular;
	if (fs::is_directory(status)) return RaFileKind::Directory;
	return RaFileKind::Other;
}

bool IsWindowsReparsePoint(const fs::path& path)
{
	const DWORD attributes = GetFileAttributesW(path.c_str());
	return attributes != INVALID_FILE_ATTRIBUTES &&
		(attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

int64_t FileTimeToUnixSeconds(const fs::file_time_type& value)
{
	const auto system_value = std::chrono::time_point_cast<
		std::chrono::system_clock::duration>(value - fs::file_time_type::clock::now() +
		std::chrono::system_clock::now());
	return std::chrono::duration_cast<std::chrono::seconds>(
		system_value.time_since_epoch()).count();
}
#endif

} // namespace

bool GetRaFileInfoNoFollow(const std::string& path, RaFileInfo *info,
	std::string *error)
{
	if (info == nullptr || path.empty()) {
		SetError(error, "invalid RA file path");
		return false;
	}
	*info = RaFileInfo();
#ifdef _WIN32
	std::error_code ec;
	fs::path native;
	if (!Utf8Path(path, &native)) {
		SetError(error, "invalid UTF-8 RA file path");
		return false;
	}
	const fs::file_status status = fs::symlink_status(native, ec);
	if (ec == std::errc::no_such_file_or_directory) return true;
	if (ec) {
		SetError(error, "cannot inspect RA file");
		return false;
	}
	if (fs::is_symlink(status) || IsWindowsReparsePoint(native)) {
		info->kind = RaFileKind::Other;
		return true;
	}
	info->kind = KindFromStatus(status);
	if (info->kind == RaFileKind::Regular) {
		info->size = fs::file_size(native, ec);
		if (ec) {
			SetError(error, "cannot determine RA file size");
			return false;
		}
		const fs::file_time_type modified = fs::last_write_time(native, ec);
		if (!ec) info->modified_time = FileTimeToUnixSeconds(modified);
	}
#else
	struct stat status = {};
	if (lstat(path.c_str(), &status) != 0) {
	#ifdef __ANDROID__
		const int descriptor = Android_GetFileDescriptor(path.c_str(), 0);
		if (descriptor >= 0) {
			const int result = fstat(descriptor, &status);
			close(descriptor);
			if (result == 0) {
				if (S_ISREG(status.st_mode)) {
					info->kind = RaFileKind::Regular;
					info->size = static_cast<uint64_t>(status.st_size);
					info->modified_time = static_cast<int64_t>(status.st_mtime);
					return true;
				}
			}
		}
	#endif
		if (errno == ENOENT) return true;
		SetError(error, "cannot inspect RA file");
		return false;
	}
	if (S_ISREG(status.st_mode)) {
		info->kind = RaFileKind::Regular;
		info->size = static_cast<uint64_t>(status.st_size);
		info->modified_time = static_cast<int64_t>(status.st_mtime);
	}
	else if (S_ISDIR(status.st_mode)) {
		info->kind = RaFileKind::Directory;
	}
	else {
		info->kind = RaFileKind::Other;
	}
#endif
	return true;
}

bool RaPathExists(const std::string& path)
{
	RaFileInfo info;
	return GetRaFileInfoNoFollow(path, &info, nullptr) &&
		info.kind != RaFileKind::Missing;
}

bool RaIsRegularFileNoFollow(const std::string& path)
{
	RaFileInfo info;
	return GetRaFileInfoNoFollow(path, &info, nullptr) &&
		info.kind == RaFileKind::Regular;
}

bool EnsureRaDirectoryTree(const std::string& path, std::string *error)
{
	if (path.empty()) {
		SetError(error, "empty RA directory path");
		return false;
	}
#ifdef _WIN32
	std::error_code ec;
	fs::path native;
	if (!Utf8Path(path, &native)) {
		SetError(error, "invalid UTF-8 RA directory path");
		return false;
	}
	fs::create_directories(native, ec);
	if (ec || !fs::is_directory(native, ec)) {
		SetError(error, "cannot create RA directory");
		return false;
	}
#else
	std::string current;
	size_t index = 0;
	if (path[0] == '/') {
		current = "/";
		index = 1;
	}
	while (index <= path.size()) {
		const size_t slash = path.find('/', index);
		const std::string part = path.substr(index,
			slash == std::string::npos ? std::string::npos : slash - index);
		if (!part.empty()) {
			if (!current.empty() && current.back() != '/') current += '/';
			current += part;
			if (mkdir(current.c_str(), 0755) != 0 && errno != EEXIST) {
				SetError(error, "cannot create RA directory");
				return false;
			}
			struct stat status = {};
			if (stat(current.c_str(), &status) != 0 || !S_ISDIR(status.st_mode)) {
				SetError(error, "RA directory path is not a directory");
				return false;
			}
		}
		if (slash == std::string::npos) break;
		index = slash + 1;
	}
#endif
	return true;
}

bool ListRaDirectoryNoFollow(const std::string& path,
	std::vector<RaDirectoryEntry> *entries, std::string *error)
{
	if (entries == nullptr || path.empty()) {
		SetError(error, "invalid RA directory listing");
		return false;
	}
	entries->clear();
#ifdef _WIN32
	std::error_code ec;
	fs::path native;
	if (!Utf8Path(path, &native)) {
		SetError(error, "invalid UTF-8 RA directory path");
		return false;
	}
	fs::directory_iterator iterator(native, ec);
	if (ec) {
		SetError(error, "cannot open RA directory");
		return false;
	}
	for (const fs::directory_entry& entry : iterator) {
		const fs::file_status status = entry.symlink_status(ec);
		if (ec) {
			ec.clear();
			continue;
		}
		RaDirectoryEntry item;
		item.path = entry.path().generic_u8string();
		item.kind = fs::is_symlink(status) || IsWindowsReparsePoint(entry.path()) ?
			RaFileKind::Other : KindFromStatus(status);
		entries->push_back(std::move(item));
	}
#else
	DIR *directory = opendir(path.c_str());
	if (directory == nullptr) {
		SetError(error, "cannot open RA directory");
		return false;
	}
	while (dirent *entry = readdir(directory)) {
		const std::string name = entry->d_name;
		if (name == "." || name == "..") continue;
		RaDirectoryEntry item;
		item.path = path;
		if (!item.path.empty() && item.path.back() != '/') item.path += '/';
		item.path += name;
		RaFileInfo info;
		if (GetRaFileInfoNoFollow(item.path, &info, nullptr)) {
			item.kind = info.kind;
			entries->push_back(std::move(item));
		}
	}
	closedir(directory);
#endif
	return true;
}

bool RemoveRaFile(const std::string& path, std::string *error)
{
#ifdef _WIN32
	std::error_code ec;
	fs::path native;
	if (!Utf8Path(path, &native)) {
		SetError(error, "invalid UTF-8 RA file path");
		return false;
	}
	fs::remove(native, ec);
	if (ec && ec != std::errc::no_such_file_or_directory) {
		SetError(error, "cannot remove RA file");
		return false;
	}
#else
	if (std::remove(path.c_str()) != 0 && errno != ENOENT) {
		SetError(error, "cannot remove RA file");
		return false;
	}
#endif
	return true;
}

bool RemoveRaTree(const std::string& path, std::string *error)
{
	RaFileInfo info;
	if (!GetRaFileInfoNoFollow(path, &info, error)) {
		return false;
	}
	if (info.kind == RaFileKind::Missing) {
		return true;
	}
	if (info.kind == RaFileKind::Directory) {
		std::vector<RaDirectoryEntry> entries;
		if (!ListRaDirectoryNoFollow(path, &entries, error)) {
			return false;
		}
		for (const RaDirectoryEntry& entry : entries) {
			if (!RemoveRaTree(entry.path, error)) {
				return false;
			}
		}
	}
	return RemoveRaFile(path, error);
}

bool MoveRaFile(const std::string& source, const std::string& destination,
	bool replace_destination, std::string *error)
{
#ifdef _WIN32
	fs::path native_source;
	fs::path native_destination;
	if (!Utf8Path(source, &native_source) ||
		!Utf8Path(destination, &native_destination)) {
		SetError(error, "invalid UTF-8 RA move path");
		return false;
	}
	DWORD flags = MOVEFILE_WRITE_THROUGH;
	if (replace_destination) flags |= MOVEFILE_REPLACE_EXISTING;
	if (MoveFileExW(native_source.c_str(), native_destination.c_str(), flags) == 0) {
		SetError(error, "cannot move RA file");
		return false;
	}
#else
	if (!replace_destination && RaPathExists(destination)) {
		SetError(error, "RA file destination already exists");
		return false;
	}
	if (std::rename(source.c_str(), destination.c_str()) != 0) {
		SetError(error, "cannot move RA file");
		return false;
	}
#endif
	return true;
}

bool CopyRaFile(const std::string& source, const std::string& destination,
	std::string *error)
{
#ifdef _WIN32
	std::error_code ec;
	fs::path native_source;
	fs::path native_destination;
	if (!Utf8Path(source, &native_source) ||
		!Utf8Path(destination, &native_destination)) {
		SetError(error, "invalid UTF-8 RA copy path");
		return false;
	}
	const bool copied = fs::copy_file(native_source, native_destination,
		fs::copy_options::none, ec);
	if (!copied || ec) {
		SetError(error, "cannot copy RA file");
		return false;
	}
#else
#ifdef __ANDROID__
	std::vector<uint8_t> source_data;
	if (!ReadRaFile(source, &source_data, std::numeric_limits<size_t>::max(), error)) {
		return false;
	}
	return WriteRaFile(destination, source_data.data(), source_data.size(), error);
#else
	std::ifstream input(source, std::ios::binary);
	std::ofstream output(destination, std::ios::binary | std::ios::trunc);
	if (!input || !output) {
		SetError(error, "cannot open RA copy streams");
		return false;
	}
	char buffer[65536];
	while (input.good()) {
		input.read(buffer, sizeof(buffer));
		const std::streamsize count = input.gcount();
		if (count > 0) output.write(buffer, count);
		if (!output) {
			SetError(error, "cannot write RA file copy");
			return false;
		}
	}
	output.flush();
	if (!output) {
		SetError(error, "cannot finish RA file copy");
		return false;
	}
#endif
#endif
	return true;
}

bool ReadRaFile(const std::string& path, std::vector<uint8_t> *data,
	size_t maximum_size, std::string *error)
{
	if (data == nullptr) {
		SetError(error, "RA file output is required");
		return false;
	}
	data->clear();
#ifdef _WIN32
	fs::path native;
	if (!Utf8Path(path, &native)) {
		SetError(error, "invalid UTF-8 RA file path");
		return false;
	}
	std::ifstream stream(native, std::ios::binary | std::ios::ate);
#else
	std::ifstream stream(path, std::ios::binary | std::ios::ate);
#endif

#ifdef __ANDROID__
	if (!stream) {
		const int descriptor = Android_GetFileDescriptor(path.c_str(), 0);
		if (descriptor >= 0) {
			struct stat status = {};
			if (fstat(descriptor, &status) == 0 && status.st_size >= 0 &&
				static_cast<uint64_t>(status.st_size) <= maximum_size &&
				static_cast<uint64_t>(status.st_size) <=
					(std::numeric_limits<size_t>::max)()) {
				data->resize(static_cast<size_t>(status.st_size));
				size_t offset = 0;
				while (offset < data->size()) {
					const ssize_t count = read(descriptor, data->data() + offset,
						data->size() - offset);
					if (count <= 0) break;
					offset += static_cast<size_t>(count);
				}
				close(descriptor);
				if (offset == data->size()) return true;
				data->clear();
			}
			else {
				close(descriptor);
			}
		}
	}
#endif
	if (!stream) {
		SetError(error, "cannot open RA file");
		return false;
	}
	const std::streamoff length = stream.tellg();
	if (length < 0 || static_cast<uint64_t>(length) > maximum_size ||
		static_cast<uint64_t>(length) > (std::numeric_limits<size_t>::max)()) {
		SetError(error, "invalid RA file size");
		return false;
	}
	data->resize(static_cast<size_t>(length));
	stream.seekg(0, std::ios::beg);
	if (!data->empty()) {
		stream.read(reinterpret_cast<char *>(data->data()),
			static_cast<std::streamsize>(data->size()));
	}
	if (!stream) {
		data->clear();
		SetError(error, "cannot read RA file");
		return false;
	}
	return true;
}

bool WriteRaFile(const std::string& path, const uint8_t *data, size_t size,
	std::string *error)
{
	if (size != 0 && data == nullptr) {
		SetError(error, "invalid RA file data");
		return false;
	}
#ifdef _WIN32
	fs::path native;
	if (!Utf8Path(path, &native)) {
		SetError(error, "invalid UTF-8 RA file path");
		return false;
	}
	std::ofstream stream(native, std::ios::binary | std::ios::trunc);
#else
	std::ofstream stream(path, std::ios::binary | std::ios::trunc);
#endif
	if (!stream) {
		SetError(error, "cannot create RA file");
		return false;
	}
	if (size != 0) {
		stream.write(reinterpret_cast<const char *>(data),
			static_cast<std::streamsize>(size));
	}
	stream.flush();
	if (!stream) {
		SetError(error, "cannot finish RA file");
		return false;
	}
	return true;
}

} // namespace Xm8Ra
