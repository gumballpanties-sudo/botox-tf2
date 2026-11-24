#pragma once
// IDA-style pattern scan over a byte range. Pure (no Windows, no SDK): tests/sigscan_test.cpp
// checks it against the plain byte-by-byte loop it replaced.
//
// Same answer as that loop -- the FIRST start offset in [0, size - n) that matches -- but it
// jumps between candidates with memchr on the first non-wildcard byte instead of testing every
// offset. ~60 sigs scan client.dll (18 MB) + engine.dll at load; a missed sig scans all of it.
#include <cstddef>
#include <cstring>
#include <vector>

namespace sigscan
{

// pattern: one int per byte, -1 = wildcard. Returns the offset, or -1 when nothing matches.
inline std::ptrdiff_t Find(const unsigned char* pData, size_t uSize, const std::vector<int>& vPattern)
{
	const size_t n = vPattern.size();
	if (!pData || uSize <= n)
		return -1;
	// a token over 0xFF (e.g. "488B" with no space) never equalled a byte in the old loop
	for (int b : vPattern)
		if (b > 0xFF || b < -1)
			return -1;

	const size_t uStarts = uSize - n;   // valid starts: 0 .. uStarts-1 (the old loop's bound)
	size_t iAnchor = 0;
	while (iAnchor < n && vPattern[iAnchor] == -1)
		iAnchor++;
	if (iAnchor == n)
		return 0;   // empty / all wildcards: the old loop matched at the first offset

	const unsigned char cAnchor = (unsigned char)vPattern[iAnchor];
	for (size_t uStart = 0; uStart < uStarts;)
	{
		auto pHit = static_cast<const unsigned char*>(memchr(pData + uStart + iAnchor, cAnchor, uStarts - uStart));
		if (!pHit)
			return -1;
		uStart = size_t(pHit - pData) - iAnchor;

		size_t j = 0;
		while (j < n && (vPattern[j] == -1 || pData[uStart + j] == (unsigned char)vPattern[j]))
			j++;
		if (j == n)
			return std::ptrdiff_t(uStart);
		uStart++;
	}
	return -1;
}

}
