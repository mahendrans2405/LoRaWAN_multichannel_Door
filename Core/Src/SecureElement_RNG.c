#include "SecureElement_RNG.h"
#include "stm32wlxx_hal.h"  // HAL header for RNG

extern RNG_HandleTypeDef hrng; // Make sure RNG is initialized in main.c or CubeMX

void SecureElementRandomNumber(uint32_t *rand)
{
    if (rand == NULL)
        return;

    // Generate random number using hardware RNG
    if (HAL_RNG_GenerateRandomNumber(&hrng, rand) != HAL_OK)
    {
        // Fallback if RNG fails: simple pseudo-random
        static uint32_t seed = 0x12345678;
        seed ^= (seed << 13);
        seed ^= (seed >> 17);
        seed ^= (seed << 5);
        *rand = seed;
    }
}
