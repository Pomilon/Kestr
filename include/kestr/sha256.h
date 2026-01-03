#ifndef KESTR_SHA256_H
#define KESTR_SHA256_H

#include <string>
#include <vector>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cstring>

namespace kestr::crypto {

    class SHA256 {
    public:
        SHA256() { reset(); }

        void update(const void* data, size_t len) {
            const uint8_t* current = static_cast<const uint8_t*>(data);
            for (size_t i = 0; i < len; ++i) {
                m_data[m_datalen++] = current[i];
                if (m_datalen == 64) {
                    transform();
                    m_bitlen += 512;
                    m_datalen = 0;
                }
            }
        }

        std::string final() {
            uint32_t i = m_datalen;

            if (m_datalen < 56) {
                m_data[i++] = 0x80;
                while (i < 56) m_data[i++] = 0x00;
            } else {
                m_data[i++] = 0x80;
                while (i < 64) m_data[i++] = 0x00;
                transform();
                memset(m_data, 0, 56);
            }

            m_bitlen += m_datalen * 8;
            m_data[63] = m_bitlen;
            m_data[62] = m_bitlen >> 8;
            m_data[61] = m_bitlen >> 16;
            m_data[60] = m_bitlen >> 24;
            m_data[59] = m_bitlen >> 32;
            m_data[58] = m_bitlen >> 40;
            m_data[57] = m_bitlen >> 48;
            m_data[56] = m_bitlen >> 56;
            transform();

            std::stringstream ss;
            for (i = 0; i < 4; ++i) {
                ss << std::hex << std::setw(8) << std::setfill('0') << m_state[0];
                ss << std::hex << std::setw(8) << std::setfill('0') << m_state[1];
                ss << std::hex << std::setw(8) << std::setfill('0') << m_state[2];
                ss << std::hex << std::setw(8) << std::setfill('0') << m_state[3];
                ss << std::hex << std::setw(8) << std::setfill('0') << m_state[4];
                ss << std::hex << std::setw(8) << std::setfill('0') << m_state[5];
                ss << std::hex << std::setw(8) << std::setfill('0') << m_state[6];
                ss << std::hex << std::setw(8) << std::setfill('0') << m_state[7];
            }
            return ss.str();
        }

        static std::string hash_file(const std::string& path) {
            std::ifstream file(path, std::ios::binary);
            if (!file) return "";
            
            SHA256 sha;
            char buffer[4096];
            while (file.read(buffer, sizeof(buffer))) {
                sha.update(buffer, file.gcount());
            }
            if (file.gcount() > 0) {
                sha.update(buffer, file.gcount());
            }
            return sha.final();
        }

    private:
        uint32_t m_state[8];
        uint8_t m_data[64];
        uint32_t m_datalen;
        uint64_t m_bitlen;

        void reset() {
            m_state[0] = 0x6a09e667;
            m_state[1] = 0xbb67ae85;
            m_state[2] = 0x3c6ef372;
            m_state[3] = 0xa54ff53a;
            m_state[4] = 0x510e527f;
            m_state[5] = 0x9b05688c;
            m_state[6] = 0x1f83d9ab;
            m_state[7] = 0x5be0cd19;
            m_datalen = 0;
            m_bitlen = 0;
            memset(m_data, 0, 64);
        }

        void transform() {
            uint32_t maj, xor_f, ch, t1, t2, a, b, c, d, e, f, g, h;
            uint32_t m[64];
            
            for (uint8_t i = 0, j = 0; i < 16; ++i, j += 4) {
                m[i] = (m_data[j] << 24) | (m_data[j + 1] << 16) | (m_data[j + 2] << 8) | (m_data[j + 3]);
            }
            for (uint8_t k = 16; k < 64; ++k) {
                m[k] = sig1(m[k - 2]) + m[k - 7] + sig0(m[k - 15]) + m[k - 16];
            }

            a = m_state[0]; b = m_state[1]; c = m_state[2]; d = m_state[3];
            e = m_state[4]; f = m_state[5]; g = m_state[6]; h = m_state[7];

            for (uint8_t i = 0; i < 64; ++i) {
                maj = (a & b) ^ (a & c) ^ (b & c);
                ch = (e & f) ^ ((~e) & g);
                t1 = h + ep1(e) + ch + k[i] + m[i];
                t2 = ep0(a) + maj;
                h = g; g = f; f = e; e = d + t1;
                d = c; c = b; b = a; a = t1 + t2;
            }

            m_state[0] += a; m_state[1] += b; m_state[2] += c; m_state[3] += d;
            m_state[4] += e; m_state[5] += f; m_state[6] += g; m_state[7] += h;
        }

        static uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }
        static uint32_t sig0(uint32_t x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }
        static uint32_t sig1(uint32_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }
        static uint32_t ep0(uint32_t x) { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }
        static uint32_t ep1(uint32_t x) { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }

        static const uint32_t k[64];
    };
}
#endif
