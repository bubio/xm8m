#include "ra_media_change_policy.h"
#include <cstdlib>
#include <iostream>

int main()
{
	Xm8Ra::RaDiskPolicyContext bank;
	bank.ra_enabled = true;
	bank.session_state = Xm8Ra::RaSessionState::Active;
	bank.role = Xm8Ra::RaDiskRole::Anchor;
	bank.same_game = true;
	bank.network_available = true;
	const Xm8Ra::RaDiskAction bank_action =
		Xm8Ra::ClassifyRaDiskAction(bank);
	Xm8Ra::RaDiskPolicyContext other_file = bank;
	if (bank_action != Xm8Ra::ClassifyRaDiskAction(other_file) ||
		bank_action != Xm8Ra::RaDiskAction::ChangeAnchorMedia) {
		std::cerr << "FAIL: bank and file media policy diverged\n";
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
