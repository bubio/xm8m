#pragma once
#include <string>
namespace MediaTestIO {
enum class Fault { None, OpenAfterProbe, OpenAfterAnalyze, ReadOnly, Close, ShortWrite };
void Inject(const std::string& path, Fault value);
bool Triggered();
void Clear();
}
