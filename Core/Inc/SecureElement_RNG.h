#ifndef SECURE_ELEMENT_RNG_H
#define SECURE_ELEMENT_RNG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Generates a random 32-bit number
 * @param  rand Pointer to the uint32_t variable to store the random number
 */
void SecureElementRandomNumber(uint32_t *rand);

#ifdef __cplusplus
}
#endif

#endif /* SECURE_ELEMENT_RNG_H */
