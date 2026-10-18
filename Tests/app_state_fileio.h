#pragma once
#include <string>

namespace StateTestIO {
enum class Fault { None, ShortWrite, Close };
// Applies only to the exact path opened for writing, never VM media or ROM reads.
void Inject(const std::string& path, Fault fault);
bool Triggered();
int Closes();
void Clear();
}
