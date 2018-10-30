// Copyright © 2018 Thomas Nagler
//
// This file is part of the RcppThread and licensed under the terms of
// the MIT license. For a copy, see the LICENSE.md file in the root directory of
// RcppThread or https://github.com/tnagler/RcppThread/blob/master/LICENSE.md.

#pragma once

#include <vector>
#include <cmath>
#include <cstddef>

namespace RcppThread {

struct Batch {
    ptrdiff_t begin;
    ptrdiff_t end;
};

inline size_t computeNumBatches(size_t nTasks, size_t nThreads)
{
    if (nTasks < nThreads)
        return nTasks;
    return nThreads * std::floor(std::sqrt(nTasks / nThreads));
}

inline std::vector<Batch> createBatches(ptrdiff_t begin,
                                        size_t nTasks,
                                        size_t nThreads,
                                        size_t nBatches)
{
    if (nTasks == 0)
        return {Batch{0, 0}};
    nThreads = std::max(nThreads, static_cast<size_t>(1));
    if (nBatches == 0)
        nBatches = computeNumBatches(nTasks, nThreads);
    nBatches = std::min(nTasks, nBatches);
    std::vector<Batch> batches(nBatches);
    size_t    minSize = nTasks / nBatches;
    ptrdiff_t remSize = nTasks % nBatches;

    for (size_t i = 0, k = 0; i < nTasks; k++) {
        ptrdiff_t bBegin = begin + i;
        ptrdiff_t bSize  = minSize + (remSize-- > 0);
        batches[k] = Batch{bBegin, bBegin + bSize};
        i += bSize;
    }

    return batches;
}

}
