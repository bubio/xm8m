// Compile the unchanged production FILEIO with a test-only SDL open adapter.
// Successful operations still use SDL's real filesystem implementation.
#include <cstring>
#include <string>
#include <SDL.h>
#include "app_state_fileio.h"

namespace {
std::string fault_path;
StateTestIO::Fault fault = StateTestIO::Fault::None;
bool triggered = false;
int closes = 0;
struct Stream {
	SDL_RWops *file;
	StateTestIO::Fault fault;
	size_t remaining = 65536; // Fail after the settings/media metadata, inside VM state.
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
	if (stream->fault == StateTestIO::Fault::ShortWrite && size != 0) {
		const size_t available = stream->remaining / size;
		if (count > available) {
			count = available;
			triggered = true;
			SDL_SetError("injected state write failure");
		}
		const size_t written = SDL_RWwrite(stream->file, data, size, count);
		stream->remaining -= written * size;
		return written;
	}
	return SDL_RWwrite(stream->file, data, size, count);
}
int SDLCALL Close(SDL_RWops *ops)
{
	auto *stream = Context(ops);
	int result = SDL_RWclose(stream->file);
	if (stream->fault == StateTestIO::Fault::Close) {
		triggered = true;
		result = SDL_SetError("injected state close failure");
	}
	++closes;
	delete stream;
	SDL_FreeRW(ops);
	return result;
}
SDL_RWops* StateRWFromFile(const char *path, const char *mode)
{
	SDL_RWops *file = SDL_RWFromFile(path, mode);
	if (!file || fault == StateTestIO::Fault::None || fault_path != path ||
		std::strcmp(mode, "wb") != 0) return file;
	SDL_RWops *ops = SDL_AllocRW();
	if (!ops) { SDL_RWclose(file); return nullptr; }
	ops->size = Size; ops->seek = Seek; ops->read = Read;
	ops->write = Write; ops->close = Close;
	ops->type = SDL_RWOPS_UNKNOWN;
	ops->hidden.unknown.data1 = new Stream{file, fault};
	return ops;
}
}
namespace StateTestIO {
void Inject(const std::string& path, Fault value)
{
	fault_path = path; fault = value; triggered = false; closes = 0;
}
bool Triggered() { return triggered; }
int Closes() { return closes; }
void Clear() { fault = Fault::None; fault_path.clear(); }
}

// SDL has already been declared above. Only calls in FILEIO are redirected;
// the adapter itself calls the real SDL function without this macro.
#define SDL_RWFromFile StateRWFromFile
#include "../Source/UI/fileio.cpp"
#undef SDL_RWFromFile
