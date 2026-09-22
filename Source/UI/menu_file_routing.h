#ifndef XM8_MENU_FILE_ROUTING_H
#define XM8_MENU_FILE_ROUTING_H

inline bool IsPlaylistEntryCommand(int current_menu_id, int command_id,
	int playlist_menu_id, int playlist_entry_min, int playlist_entry_max)
{
	return current_menu_id == playlist_menu_id &&
		command_id >= playlist_entry_min && command_id <= playlist_entry_max;
}

#endif
