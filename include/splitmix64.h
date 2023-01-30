//
// Created by Sam Thompson on 14/03/2019.
//

#ifndef RNG_TESTING_SPLITMIX64_H
#define RNG_TESTING_SPLITMIX64_H

#include <cstdint>

/**
 * @brief A random number generator using the splitmix64 algorithm - this is provided for generating the shuffle table
 * within the main Xoroshiro256+ algorithm.
 */
class SplitMix64
{
private:
    uint64_t x{}; /* The state can be seeded with any value. */

public:

    /**
     * @brief Default constructor, taking the RNG seed.
     * @param seed the seed to use
     */
    explicit SplitMix64(uint64_t seed) : x(seed){}

    /**
     * @brief Generates the next random integer
     * @return a random integer in the range of 0 to 2^64
     */
    uint64_t next()
    {
        uint64_t z = (x += UINT64_C(0x9E3779B97F4A7C15));
        z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
        z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
        return z ^ (z >> 31);
    }

    /**
     * @brief Shuffle the random number 8 times.
     */
    void shuffle()
    {
        for(unsigned int i = 0; i < 8; i++)
        {
            next();
        }
    }
};

#endif //RNG_TESTING_SPLITMIX64_H