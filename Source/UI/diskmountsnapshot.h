#ifndef XM8_DISK_MOUNT_SNAPSHOT_H
#define XM8_DISK_MOUNT_SNAPSHOT_H

#include <string>
#include "diskmgr.h"

// Local drive state only. RA lifecycle, persistence and reset remain owned by
// the caller. Capturing a drive never probes, opens or registers its media.
class DiskMountSnapshots {
public:
	struct Drive {
		bool open = false;
		std::string path;
		int bank = 0;
	};

	// An empty snapshot is a no-op until assigned a captured target set.
	DiskMountSnapshots() = default;

	DiskMountSnapshots(DiskManager* const drives[], int first = 0, int last = 1)
		: first_(first), last_(last)
	{
		SDL_assert(first >= 0 && first <= last && last < 2);
		for (int drive = first_; drive <= last_; ++drive) {
			Drive& saved = drives_[drive];
			saved.open = drives[drive]->IsOpen();
			if (saved.open) {
				saved.path = drives[drive]->GetPath();
				saved.bank = drives[drive]->GetBank();
			}
		}
	}

	const Drive& operator[](int drive) const { return drives_[drive]; }

	bool Restore(DiskManager* const drives[]) const
	{
		bool restored = true;
		for (int drive = first_; drive <= last_; ++drive) {
			const Drive& saved = drives_[drive];
			if (saved.open) {
				// Always attempt every selected drive, even after a failure.
				restored = drives[drive]->Open(saved.path.c_str(), saved.bank)
					&& restored;
			}
			else {
				drives[drive]->Close();
			}
		}
		return restored;
	}

private:
	Drive drives_[2];
	int first_ = 0;
	int last_ = -1;
};

#endif
