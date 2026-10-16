#pragma once

// Monotonic intro barrier: 0 wait for arrivals, 1 preload, 2 play, 3 release.
// A seat reports 0 arrived, 1 loaded, 2 finished. Removed seats do not hold it.
inline unsigned char zm_bumper_barrier_phase(unsigned char phase,
                                             const signed char* ready,
                                             unsigned int seats)
{
    while (phase < 3) {
        bool all = true;
        for (int i = 0; i < 4; ++i)
            if ((seats & (1u << i)) && ready[i] < (int)phase) all = false;
        if (!all) break;
        ++phase;
    }
    return phase;
}
