//
// eXcellent Multi-platform emulator type 8 - 'XM8'
// based on ePC-8801MA
//
// Author (ePC-8801MA) : Takeda.Toshiya
// Author (XM8) : Tanaka.Yasushi
//
// [ tape manager ]
//

#ifdef SDL

#include "os.h"
#include "common.h"
#include "vm.h"
#include "app.h"
#include "pathresolver.h"
#include "tapemgr.h"

//
// TapeManager()
// constructor
//
TapeManager::TapeManager()
{
	// object
	vm = NULL;

	// mount status
	mount_play = false;
	mount_rec = false;

	// path and dir
	path[0] = '\0';
	resolved_path[0] = '\0';
	dir[0] = '\0';
	state_path[0] = '\0';
	nullstr[0] = '\0';
}

//
// ~TapeManager()
// destructor
//
TapeManager::~TapeManager()
{
	Deinit();
}

//
// Init()
// initialize
//
bool TapeManager::Init(VM *v)
{
	// save object
	vm = v;

	return true;
}

//
// Deinit()
// deinitialize
//
void TapeManager::Deinit()
{
	// eject current tape
	if ((mount_play == true) || (mount_rec == true)) {
		Eject();
	}
}

//
// SetVM()
// re-set vm
//
void TapeManager::SetVM(VM *v)
{
	// save object
	vm = v;
}

//
// Play()
// play tape
//
bool TapeManager::Play(const char *filename)
{
	// open
	if (Open(filename, false) == false) {
		return false;
	}

	// Eject
	Eject();

	// virtual machine
	vm->play_tape((_TCHAR*)resolved_path);

	// mount ok
	mount_play = true;

	return true;
}

//
// Rec()
// rec tape
//
bool TapeManager::Rec(const char *filename)
{
	// open
	if (Open(filename, true) == false) {
		return false;
	}

	// Eject
	Eject();

	// virtual machine
	vm->rec_tape((_TCHAR*)resolved_path);

	// The VM can fail its own open after the preflight succeeded.
	mount_rec = vm->tape_inserted();

	return mount_rec;
}

//
// Open()
// open and close
//
bool TapeManager::Open(const char *filename, bool rec)
{
	char *ptr;
	char *last;
	char candidate_path[_MAX_PATH * 3];
	char candidate_dir[_MAX_PATH * 3];
	char candidate_resolved[_MAX_PATH * 3];
	FILEIO fileio;
	bool ret;

	const char *source = filename != NULL ? filename : path;
	if (source[0] == '\0' || strlen(source) >= sizeof(candidate_path)) {
		return false;
	}
	strcpy(candidate_path, source);
	strcpy(candidate_dir, candidate_path);
	ptr = candidate_dir;
	last = candidate_dir;

	// search last '\\' or '/'
	while (*ptr != '\0') {
		if ((*ptr == '\\') || (*ptr == '/')) {
			last = ptr;
		}
		ptr++;
	}
	last[1] = '\0';

	if (ResolvePathForIO(candidate_path, candidate_resolved,
		sizeof(candidate_resolved)) == false) {
		return false;
	}

	// open test
	if (rec == true) {
		ret = fileio.Fopen(candidate_resolved,
			FILEIO_READ_WRITE_NEW_BINARY);
	}
	else {
		ret = fileio.Fopen(candidate_resolved, FILEIO_READ_BINARY);
	}
	if (ret == true) {
		// close immediately
		fileio.Fclose();
		if (fileio.HasError()) {
			return false;
		}
		strcpy(path, candidate_path);
		strcpy(dir, candidate_dir);
		strcpy(resolved_path, candidate_resolved);
	}

	return ret;
}

//
// Eject()
// eject tape
//
void TapeManager::Eject()
{
	// close
	if ((mount_play == true) || (mount_rec == true)) {
		vm->close_tape();

		// mount flag
		mount_play = false;
		mount_rec = false;
	}
}

//
// IsPlay()
// check tape mount (play)
//
bool TapeManager::IsPlay()
{
	return mount_play;
}

//
// IsRec()
// check tape mount (rec)
//
bool TapeManager::IsRec()
{
	return mount_rec;
}

//
// GetDir()
// get tape dir
//
const char* TapeManager::GetDir()
{
	return dir;
}

//
// GetFileName()
// get tape file name
//
const char* TapeManager::GetFileName()
{
	size_t len;

	// open ?
	if ((mount_play == false) && (mount_rec == false)) {
		return nullstr;
	}

	// get length of directory
	len = strlen(dir);
	SDL_assert(len > 0);
	SDL_assert(strlen(path) > len);

	return &path[len];
}

//
// Load()
// load state
//
void TapeManager::Load(FILEIO *fio)
{
	bool play;
	bool rec;

	// eject
	Eject();

	fio->Fread(state_path, 1, sizeof(state_path));
	play = fio->FgetBool();
	rec = fio->FgetBool();

	if (play == true) {
		Play(state_path);
	}
	if (rec == true) {
		Rec(state_path);
	}
}

//
// Save()
// save state
//
void TapeManager::Save(FILEIO *fio)
{
	fio->Fwrite(path, 1, sizeof(path));
	fio->FputBool(mount_play);
	fio->FputBool(mount_rec);
}

#endif // SDL
