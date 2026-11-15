#ifndef XM8_RA_HMAC_SHA256_H
#define XM8_RA_HMAC_SHA256_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Xm8Ra {

// SHA-256 (FIPS 180-4) and HMAC (RFC 2104). Header only so that no platform
// build definition has to list another source file.
class RaSha256 {
public:
	using Digest = std::array<uint8_t, 32>;

	RaSha256()
	{
		static const uint32_t initial[8] = {
			0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
			0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
		};
		for (int index = 0; index < 8; ++index) state_[index] = initial[index];
	}

	void Update(const void *data, size_t size)
	{
		const uint8_t *bytes = static_cast<const uint8_t *>(data);
		total_bytes_ += size;
		while (size != 0) {
			block_[block_size_++] = *bytes++;
			--size;
			if (block_size_ == sizeof(block_)) {
				Compress();
				block_size_ = 0;
			}
		}
	}

	Digest Finish()
	{
		const uint64_t total_bits = total_bytes_ * 8u;
		const uint8_t one = 0x80;
		const uint8_t zero = 0;
		Update(&one, 1);
		while (block_size_ != 56) Update(&zero, 1);
		uint8_t length[8];
		for (int index = 0; index < 8; ++index) {
			length[index] = static_cast<uint8_t>(total_bits >> (56 - index * 8));
		}
		Update(length, sizeof(length));
		Digest digest;
		for (int index = 0; index < 8; ++index) {
			digest[index * 4] = static_cast<uint8_t>(state_[index] >> 24);
			digest[index * 4 + 1] = static_cast<uint8_t>(state_[index] >> 16);
			digest[index * 4 + 2] = static_cast<uint8_t>(state_[index] >> 8);
			digest[index * 4 + 3] = static_cast<uint8_t>(state_[index]);
		}
		return digest;
	}

private:
	static uint32_t RotateRight(uint32_t value, int bits)
	{
		return (value >> bits) | (value << (32 - bits));
	}

	void Compress()
	{
		static const uint32_t round_constants[64] = {
			0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
			0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
			0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
			0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
			0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
			0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
			0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
			0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
			0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
			0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
			0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
			0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
			0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
			0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
			0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
			0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
		};
		uint32_t schedule[64];
		for (int index = 0; index < 16; ++index) {
			schedule[index] =
				(static_cast<uint32_t>(block_[index * 4]) << 24) |
				(static_cast<uint32_t>(block_[index * 4 + 1]) << 16) |
				(static_cast<uint32_t>(block_[index * 4 + 2]) << 8) |
				static_cast<uint32_t>(block_[index * 4 + 3]);
		}
		for (int index = 16; index < 64; ++index) {
			const uint32_t s0 = RotateRight(schedule[index - 15], 7) ^
				RotateRight(schedule[index - 15], 18) ^
				(schedule[index - 15] >> 3);
			const uint32_t s1 = RotateRight(schedule[index - 2], 17) ^
				RotateRight(schedule[index - 2], 19) ^
				(schedule[index - 2] >> 10);
			schedule[index] = schedule[index - 16] + s0 +
				schedule[index - 7] + s1;
		}
		uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
		uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
		for (int index = 0; index < 64; ++index) {
			const uint32_t sum1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^
				RotateRight(e, 25);
			const uint32_t choice = (e & f) ^ (~e & g);
			const uint32_t temp1 = h + sum1 + choice +
				round_constants[index] + schedule[index];
			const uint32_t sum0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^
				RotateRight(a, 22);
			const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
			const uint32_t temp2 = sum0 + majority;
			h = g;
			g = f;
			f = e;
			e = d + temp1;
			d = c;
			c = b;
			b = a;
			a = temp1 + temp2;
		}
		state_[0] += a;
		state_[1] += b;
		state_[2] += c;
		state_[3] += d;
		state_[4] += e;
		state_[5] += f;
		state_[6] += g;
		state_[7] += h;
	}

	uint32_t state_[8];
	uint8_t block_[64] = {};
	size_t block_size_ = 0;
	uint64_t total_bytes_ = 0;
};

inline RaSha256::Digest RaHmacSha256(const std::string& key,
	const std::string& message)
{
	uint8_t block_key[64] = {};
	if (key.size() > sizeof(block_key)) {
		RaSha256 hash;
		hash.Update(key.data(), key.size());
		const RaSha256::Digest digest = hash.Finish();
		for (size_t index = 0; index < digest.size(); ++index) {
			block_key[index] = digest[index];
		}
	}
	else {
		for (size_t index = 0; index < key.size(); ++index) {
			block_key[index] = static_cast<uint8_t>(key[index]);
		}
	}
	uint8_t inner_pad[64];
	uint8_t outer_pad[64];
	for (size_t index = 0; index < sizeof(block_key); ++index) {
		inner_pad[index] = block_key[index] ^ 0x36;
		outer_pad[index] = block_key[index] ^ 0x5c;
	}
	RaSha256 inner;
	inner.Update(inner_pad, sizeof(inner_pad));
	inner.Update(message.data(), message.size());
	const RaSha256::Digest inner_digest = inner.Finish();
	RaSha256 outer;
	outer.Update(outer_pad, sizeof(outer_pad));
	outer.Update(inner_digest.data(), inner_digest.size());
	const RaSha256::Digest digest = outer.Finish();
	volatile uint8_t *wipe = block_key;
	for (size_t index = 0; index < sizeof(block_key); ++index) wipe[index] = 0;
	return digest;
}

inline std::string RaHexDigest(const RaSha256::Digest& digest)
{
	static const char digits[] = "0123456789abcdef";
	std::string text;
	text.reserve(digest.size() * 2);
	for (uint8_t byte : digest) {
		text.push_back(digits[byte >> 4]);
		text.push_back(digits[byte & 0x0f]);
	}
	return text;
}

// Comparison whose duration does not depend on where the texts differ.
inline bool RaConstantTimeEquals(const std::string& left,
	const std::string& right)
{
	if (left.size() != right.size()) return false;
	unsigned char difference = 0;
	for (size_t index = 0; index < left.size(); ++index) {
		difference |= static_cast<unsigned char>(left[index] ^ right[index]);
	}
	return difference == 0;
}

} // namespace Xm8Ra

#endif
