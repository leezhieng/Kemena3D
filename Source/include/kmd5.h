#ifndef KMD5_H_INCLUDED
#define KMD5_H_INCLUDED

// kmd5.h
#pragma once
#include <string>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <cstdint>

namespace kemena
{

/**
 * @brief Streaming MD5 message-digest implementation.
 *
 * Computes the 128-bit MD5 hash of arbitrary byte data following RFC 1321.
 * Data may be fed incrementally through update() and the final hexadecimal
 * digest retrieved with final(). For one-shot hashing of a string, use the
 * static hash() convenience method.
 *
 * @note MD5 is cryptographically broken and should not be used for security
 *       purposes; it is provided for checksums and non-security identifiers.
 */
class kMD5 {
public:
    /** @brief Constructs the context and initialises it to the MD5 starting state. */
    kMD5() { init(); }

    /**
     * @brief Feeds a block of raw bytes into the running digest.
     * @param input  Pointer to the input bytes.
     * @param length Number of bytes to process.
     */
    void update(const uint8_t* input, size_t length);

    /**
     * @brief Feeds the contents of a string into the running digest.
     * @param input String whose bytes are appended to the message.
     */
    void update(const std::string& input);

    /**
     * @brief Finalises the digest and returns it as a lowercase hex string.
     * @return The 32-character hexadecimal MD5 digest.
     */
    std::string final();

    /**
     * @brief One-shot helper that hashes an entire string.
     * @param input The string to hash.
     * @return The 32-character hexadecimal MD5 digest of @p input.
     */
    static std::string hash(const std::string& input);

private:
    /** @brief Resets the bit count and seeds the four MD5 state words. */
    void init();

    /**
     * @brief Processes a single 64-byte block, updating the internal state.
     * @param block The 64-byte input block to mix into the state.
     */
    void transform(const uint8_t block[64]);

    uint32_t state[4];   ///< Running MD5 state words (A, B, C, D).
    uint64_t count;      ///< Total number of message bits processed so far.
    uint8_t buffer[64];  ///< Partial-block buffer holding bytes not yet transformed.
};

/**
 * @brief MD5 auxiliary function F used in round 1: (x & y) | (~x & z).
 * @param x First state word.
 * @param y Second state word.
 * @param z Third state word.
 * @return The bitwise result of the F mixing function.
 */
inline uint32_t F(uint32_t x, uint32_t y, uint32_t z) { return (x & y) | (~x & z); }

/**
 * @brief MD5 auxiliary function G used in round 2: (x & z) | (y & ~z).
 * @param x First state word.
 * @param y Second state word.
 * @param z Third state word.
 * @return The bitwise result of the G mixing function.
 */
inline uint32_t G(uint32_t x, uint32_t y, uint32_t z) { return (x & z) | (y & ~z); }

/**
 * @brief MD5 auxiliary function H used in round 3: x ^ y ^ z.
 * @param x First state word.
 * @param y Second state word.
 * @param z Third state word.
 * @return The bitwise XOR of the three state words.
 */
inline uint32_t H(uint32_t x, uint32_t y, uint32_t z) { return x ^ y ^ z; }

/**
 * @brief MD5 auxiliary function I used in round 4: y ^ (x | ~z).
 * @param x First state word.
 * @param y Second state word.
 * @param z Third state word.
 * @return The bitwise result of the I mixing function.
 */
inline uint32_t I(uint32_t x, uint32_t y, uint32_t z) { return y ^ (x | ~z); }

/**
 * @brief Performs a circular left rotation of a 32-bit value.
 * @param x Value to rotate.
 * @param n Number of bit positions to rotate left.
 * @return @p x rotated left by @p n bits.
 */
inline uint32_t rotate_left(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

/**
 * @brief Round-1 transformation step using the F mixing function.
 * @param a  State word updated in place.
 * @param b  Second state word.
 * @param c  Third state word.
 * @param d  Fourth state word.
 * @param x  Message sub-block word for this step.
 * @param s  Left-rotation amount.
 * @param ac Per-step additive constant.
 */
inline void FF(uint32_t &a, uint32_t b, uint32_t c, uint32_t d,
               uint32_t x, uint32_t s, uint32_t ac)
{
    a += F(b, c, d) + x + ac;
    a = rotate_left(a, s);
    a += b;
}

/**
 * @brief Round-2 transformation step using the G mixing function.
 * @param a  State word updated in place.
 * @param b  Second state word.
 * @param c  Third state word.
 * @param d  Fourth state word.
 * @param x  Message sub-block word for this step.
 * @param s  Left-rotation amount.
 * @param ac Per-step additive constant.
 */
inline void GG(uint32_t &a, uint32_t b, uint32_t c, uint32_t d,
               uint32_t x, uint32_t s, uint32_t ac)
{
    a += G(b, c, d) + x + ac;
    a = rotate_left(a, s);
    a += b;
}

/**
 * @brief Round-3 transformation step using the H mixing function.
 * @param a  State word updated in place.
 * @param b  Second state word.
 * @param c  Third state word.
 * @param d  Fourth state word.
 * @param x  Message sub-block word for this step.
 * @param s  Left-rotation amount.
 * @param ac Per-step additive constant.
 */
inline void HH(uint32_t &a, uint32_t b, uint32_t c, uint32_t d,
               uint32_t x, uint32_t s, uint32_t ac)
{
    a += H(b, c, d) + x + ac;
    a = rotate_left(a, s);
    a += b;
}

/**
 * @brief Round-4 transformation step using the I mixing function.
 * @param a  State word updated in place.
 * @param b  Second state word.
 * @param c  Third state word.
 * @param d  Fourth state word.
 * @param x  Message sub-block word for this step.
 * @param s  Left-rotation amount.
 * @param ac Per-step additive constant.
 */
inline void II(uint32_t &a, uint32_t b, uint32_t c, uint32_t d,
               uint32_t x, uint32_t s, uint32_t ac)
{
    a += I(b, c, d) + x + ac;
    a = rotate_left(a, s);
    a += b;
}

/**
 * @brief Serialises 32-bit words into bytes in little-endian order.
 * @param output Destination byte buffer (must hold @p length bytes).
 * @param input  Source array of 32-bit words.
 * @param length Number of output bytes to produce (multiple of 4).
 */
inline void encode(uint8_t* output, const uint32_t* input, size_t length) {
    for (size_t i = 0, j = 0; j < length; ++i, j += 4) {
        output[j] = input[i] & 0xff;
        output[j + 1] = (input[i] >> 8) & 0xff;
        output[j + 2] = (input[i] >> 16) & 0xff;
        output[j + 3] = (input[i] >> 24) & 0xff;
    }
}

/**
 * @brief Reassembles bytes into 32-bit words assuming little-endian order.
 * @param output Destination array of 32-bit words (holds @p length / 4 words).
 * @param input  Source byte buffer.
 * @param length Number of input bytes to consume (multiple of 4).
 */
inline void decode(uint32_t* output, const uint8_t* input, size_t length) {
    for (size_t i = 0, j = 0; j < length; ++i, j += 4) {
        output[i] = ((uint32_t)input[j]) |
                    (((uint32_t)input[j + 1]) << 8) |
                    (((uint32_t)input[j + 2]) << 16) |
                    (((uint32_t)input[j + 3]) << 24);
    }
}

inline void kMD5::init() {
    count = 0;
    state[0] = 0x67452301;
    state[1] = 0xefcdab89;
    state[2] = 0x98badcfe;
    state[3] = 0x10325476;
}

inline void kMD5::update(const uint8_t* input, size_t length) {
    size_t i = 0, index = (size_t)((count >> 3) & 0x3F);
    count += ((uint64_t)length) << 3;

    size_t partLen = 64 - index;

    if (length >= partLen) {
        std::memcpy(&buffer[index], input, partLen);
        transform(buffer);
        for (i = partLen; i + 63 < length; i += 64)
            transform(&input[i]);
        index = 0;
    } else {
        i = 0;
    }
    std::memcpy(&buffer[index], &input[i], length - i);
}

inline void kMD5::update(const std::string& input) {
    update(reinterpret_cast<const uint8_t*>(input.c_str()), input.length());
}

inline std::string kMD5::final() {
    static uint8_t PADDING[64] = { 0x80 };
    uint8_t bits[8];
    encode(bits, reinterpret_cast<uint32_t*>(&count), 8);

    size_t index = (size_t)((count >> 3) & 0x3f);
    size_t padLen = (index < 56) ? (56 - index) : (120 - index);
    update(PADDING, padLen);
    update(bits, 8);

    uint8_t digest[16];
    encode(digest, state, 16);

    std::ostringstream oss;
    for (int i = 0; i < 16; ++i)
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)digest[i];
    return oss.str();
}

inline std::string kMD5::hash(const std::string& input) {
    kMD5 md5;
    md5.update(input);
    return md5.final();
}

inline void kMD5::transform(const uint8_t block[64]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], x[16];
    decode(x, block, 64);

    FF(a, b, c, d, x[ 0],  7, 0xd76aa478);
    FF(d, a, b, c, x[ 1], 12, 0xe8c7b756);
    FF(c, d, a, b, x[ 2], 17, 0x242070db);
    FF(b, c, d, a, x[ 3], 22, 0xc1bdceee);
    FF(a, b, c, d, x[ 4],  7, 0xf57c0faf);
    FF(d, a, b, c, x[ 5], 12, 0x4787c62a);
    FF(c, d, a, b, x[ 6], 17, 0xa8304613);
    FF(b, c, d, a, x[ 7], 22, 0xfd469501);
    FF(a, b, c, d, x[ 8],  7, 0x698098d8);
    FF(d, a, b, c, x[ 9], 12, 0x8b44f7af);
    FF(c, d, a, b, x[10], 17, 0xffff5bb1);
    FF(b, c, d, a, x[11], 22, 0x895cd7be);
    FF(a, b, c, d, x[12],  7, 0x6b901122);
    FF(d, a, b, c, x[13], 12, 0xfd987193);
    FF(c, d, a, b, x[14], 17, 0xa679438e);
    FF(b, c, d, a, x[15], 22, 0x49b40821);

    GG(a, b, c, d, x[ 1],  5, 0xf61e2562);
    GG(d, a, b, c, x[ 6],  9, 0xc040b340);
    GG(c, d, a, b, x[11], 14, 0x265e5a51);
    GG(b, c, d, a, x[ 0], 20, 0xe9b6c7aa);
    GG(a, b, c, d, x[ 5],  5, 0xd62f105d);
    GG(d, a, b, c, x[10],  9, 0x02441453);
    GG(c, d, a, b, x[15], 14, 0xd8a1e681);
    GG(b, c, d, a, x[ 4], 20, 0xe7d3fbc8);
    GG(a, b, c, d, x[ 9],  5, 0x21e1cde6);
    GG(d, a, b, c, x[14],  9, 0xc33707d6);
    GG(c, d, a, b, x[ 3], 14, 0xf4d50d87);
    GG(b, c, d, a, x[ 8], 20, 0x455a14ed);
    GG(a, b, c, d, x[13],  5, 0xa9e3e905);
    GG(d, a, b, c, x[ 2],  9, 0xfcefa3f8);
    GG(c, d, a, b, x[ 7], 14, 0x676f02d9);
    GG(b, c, d, a, x[12], 20, 0x8d2a4c8a);

    HH(a, b, c, d, x[ 5],  4, 0xfffa3942);
    HH(d, a, b, c, x[ 8], 11, 0x8771f681);
    HH(c, d, a, b, x[11], 16, 0x6d9d6122);
    HH(b, c, d, a, x[14], 23, 0xfde5380c);
    HH(a, b, c, d, x[ 1],  4, 0xa4beea44);
    HH(d, a, b, c, x[ 4], 11, 0x4bdecfa9);
    HH(c, d, a, b, x[ 7], 16, 0xf6bb4b60);
    HH(b, c, d, a, x[10], 23, 0xbebfbc70);
    HH(a, b, c, d, x[13],  4, 0x289b7ec6);
    HH(d, a, b, c, x[ 0], 11, 0xeaa127fa);
    HH(c, d, a, b, x[ 3], 16, 0xd4ef3085);
    HH(b, c, d, a, x[ 6], 23, 0x04881d05);
    HH(a, b, c, d, x[ 9],  4, 0xd9d4d039);
    HH(d, a, b, c, x[12], 11, 0xe6db99e5);
    HH(c, d, a, b, x[15], 16, 0x1fa27cf8);
    HH(b, c, d, a, x[ 2], 23, 0xc4ac5665);

    II(a, b, c, d, x[ 0],  6, 0xf4292244);
    II(d, a, b, c, x[ 7], 10, 0x432aff97);
    II(c, d, a, b, x[14], 15, 0xab9423a7);
    II(b, c, d, a, x[ 5], 21, 0xfc93a039);
    II(a, b, c, d, x[12],  6, 0x655b59c3);
    II(d, a, b, c, x[ 3], 10, 0x8f0ccc92);
    II(c, d, a, b, x[10], 15, 0xffeff47d);
    II(b, c, d, a, x[ 1], 21, 0x85845dd1);
    II(a, b, c, d, x[ 8],  6, 0x6fa87e4f);
    II(d, a, b, c, x[15], 10, 0xfe2ce6e0);
    II(c, d, a, b, x[ 6], 15, 0xa3014314);
    II(b, c, d, a, x[13], 21, 0x4e0811a1);
    II(a, b, c, d, x[ 4],  6, 0xf7537e82);
    II(d, a, b, c, x[11], 10, 0xbd3af235);
    II(c, d, a, b, x[ 2], 15, 0x2ad7d2bb);
    II(b, c, d, a, x[ 9], 21, 0xeb86d391);

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

} // namespace kemena

#endif // KMD5_H_INCLUDED
