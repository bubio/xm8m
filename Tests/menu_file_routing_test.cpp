#include "menu_file_routing.h"

#include <cstdlib>
#include <iostream>

int main()
{
	constexpr int playlist_menu = 46;
	constexpr int file_menu = 20;
	constexpr int playlist_min = 11000;
	constexpr int playlist_max = 11999;

	if (IsPlaylistEntryCommand(file_menu, 11000, playlist_menu,
		playlist_min, playlist_max)) {
		std::cerr << "FAIL: a large-directory file was routed as a playlist entry\n";
		return EXIT_FAILURE;
	}
	if (!IsPlaylistEntryCommand(playlist_menu, 11000, playlist_menu,
		playlist_min, playlist_max)) {
		std::cerr << "FAIL: a real playlist entry was not routed to the playlist\n";
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
