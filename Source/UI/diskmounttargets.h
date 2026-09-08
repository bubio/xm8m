#ifndef XM8_DISK_MOUNT_TARGETS_H
#define XM8_DISK_MOUNT_TARGETS_H

#include "clidisk.h"
#include "diskmountsnapshot.h"

// Prepared VM destinations, independent of RA identity and request entry point.
// The caller validates every Mount before Apply, owns persistence/reset, and
// decides whether a failed Apply needs Restore. Keep never touches a drive.
class DiskMountTargets {
public:
	enum class Action { Keep, Eject, Mount };
	struct Drive {
		Action action = Action::Keep;
		std::string path;
		int bank = 0;
	};

	void Mount(const DiskSpec& spec)
	{
		SDL_assert(spec.drive >= 0 && spec.drive < 2);
		drives_[spec.drive] = {Action::Mount, spec.path, spec.bank};
	}
	void Eject(int drive)
	{
		SDL_assert(drive >= 0 && drive < 2);
		drives_[drive] = {Action::Eject, "", 0};
	}
	const Drive& operator[](int drive) const { return drives_[drive]; }
	bool Changes(int drive) const { return drives_[drive].action != Action::Keep; }
	bool IsPair() const { return Changes(0) && Changes(1); }

	DiskMountSnapshots Capture(DiskManager* const drives[]) const
	{
		if (!Changes(0) && !Changes(1)) return DiskMountSnapshots();
		return DiskMountSnapshots(drives, Changes(0) ? 0 : 1, Changes(1) ? 1 : 0);
	}

	bool Apply(DiskManager* const drives[]) const
	{
		for (int drive = 0; drive < 2; ++drive) {
			const Drive& target = drives_[drive];
			switch (target.action) {
			case Action::Keep: break;
			case Action::Eject: drives[drive]->Close(); break;
			case Action::Mount:
				if (!drives[drive]->Open(target.path.c_str(), target.bank)) return false;
				break;
			}
		}
		return true;
	}

private:
	Drive drives_[2];
};

#endif
