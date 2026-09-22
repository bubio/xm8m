#include "ra_media_change_policy.h"
#include <cstdlib>
#include <iostream>

int main()
{
	Xm8Ra::RaDiskPolicyContext input;
	input.ra_enabled = true;
	input.session_state = Xm8Ra::RaSessionState::Active;
	input.role = Xm8Ra::RaDiskRole::Auxiliary;
	input.network_available = true;
	if (Xm8Ra::ClassifyRaDiskAction(input) !=
		Xm8Ra::RaDiskAction::VerifyAuxiliary) {
		std::cerr << "FAIL: Drive 2 policy is not mode-independent\n";
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
