// Compile the unchanged production FILEIO with a test-only SDL open adapter.
// Successful operations still use SDL's real filesystem implementation.
#include <cstring>
#include <string>
#include <SDL.h>
#include "media_manager_fileio.h"
#include "pathresolver.h"

namespace {
std::string fault_path;
MediaTestIO::Fault fault = MediaTestIO::Fault::None;
bool triggered = false;
int opens = 0;
struct Stream {
	SDL_RWops *file;
	MediaTestIO::Fault fault;
	size_t remaining = 2; // Limit the actual recording flush to two bytes.
};
Stream* Context(SDL_RWops *ops) { return static_cast<Stream*>(ops->hidden.unknown.data1); }
Sint64 SDLCALL Size(SDL_RWops *ops) { return SDL_RWsize(Context(ops)->file); }
Sint64 SDLCALL Seek(SDL_RWops *ops, Sint64 offset, int whence)
{
	return SDL_RWseek(Context(ops)->file, offset, whence);
}
size_t SDLCALL Read(SDL_RWops *ops, void *data, size_t size, size_t count)
{
	return SDL_RWread(Context(ops)->file, data, size, count);
}
size_t SDLCALL Write(SDL_RWops *ops, const void *data, size_t size, size_t count)
{
	auto *stream = Context(ops);
	if (stream->fault == MediaTestIO::Fault::ShortWrite && size != 0) {
		const size_t requested = size * count;
		const size_t allowed = requested < stream->remaining ? requested : stream->remaining;
		const size_t written = SDL_RWwrite(stream->file, data, 1, allowed);
		stream->remaining -= written;
		if (written < requested) {
			triggered = true;
			SDL_SetError("injected media short write");
		}
		return written / size;
	}
	return SDL_RWwrite(stream->file, data, size, count);
}
int SDLCALL Close(SDL_RWops *ops)
{
	auto *stream = Context(ops);
	int result = SDL_RWclose(stream->file);
	if (stream->fault == MediaTestIO::Fault::Close) {
		triggered = true;
		result = SDL_SetError("injected media close failure");
	}
	delete stream;
	SDL_FreeRW(ops);
	return result;
}
SDL_RWops* MediaRWFromFile(const char *path, const char *mode)
{
	if (fault_path == path) {
		++opens;
		if ((fault == MediaTestIO::Fault::OpenAfterProbe && opens > 1) ||
			(fault == MediaTestIO::Fault::OpenAfterAnalyze && opens > 2) ||
			(fault == MediaTestIO::Fault::ReadOnly && std::strcmp(mode, "rb") != 0)) {
			triggered = true;
			SDL_SetError("injected media open failure");
			return nullptr;
		}
	}
	SDL_RWops *file = SDL_RWFromFile(path, mode);
	if (!file || fault_path != path ||
		(fault != MediaTestIO::Fault::Close && fault != MediaTestIO::Fault::ShortWrite)) return file;
	SDL_RWops *ops = SDL_AllocRW();
	if (!ops) { SDL_RWclose(file); return nullptr; }
	ops->size = Size; ops->seek = Seek; ops->read = Read;
	ops->write = Write; ops->close = Close;
	ops->type = SDL_RWOPS_UNKNOWN;
	ops->hidden.unknown.data1 = new Stream{file, fault};
	return ops;
}
}
namespace MediaTestIO {
void Inject(const std::string& path, Fault value)
{
	char resolved[4096];
	fault_path = ResolvePathForIO(path.c_str(), resolved, sizeof(resolved)) ? resolved : path;
	fault = value; triggered = false; opens = 0;
}
bool Triggered() { return triggered; }
void Clear() { fault = Fault::None; fault_path.clear(); }
}

// SDL has already been declared above. Only calls in FILEIO are redirected;
// the adapter itself calls the real SDL function without this macro.
#define SDL_RWFromFile MediaRWFromFile
#include "../Source/UI/fileio.cpp"
#undef SDL_RWFromFile
