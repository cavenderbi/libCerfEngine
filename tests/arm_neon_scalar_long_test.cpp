#include "arm_ir_rig.h"
#include "../src/arm/arm_neon_2regscalar.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>

namespace {

using Scalar = cerfengine::ArmNeon2RegScalar;
constexpr std::array<uint32_t, 3> kOps = {Scalar::kS2sMlal, Scalar::kS2sMlsl, Scalar::kS2sMull};

uint64_t Mask(uint32_t bits) {
    return bits == 64u ? UINT64_MAX : (uint64_t{1} << bits) - 1u;
}

void Check(ArmIrRig& rig, std::mt19937_64& rng, uint32_t op, uint32_t size,
           uint32_t u, uint32_t d, uint32_t n, uint32_t m, uint32_t index, uint32_t pattern) {
    auto& state = *rig.Cpu().State();
    for (auto& value : state.vfp_d) value = rng();
    if (pattern < 6u) {
        constexpr uint64_t values[] = {0u, UINT64_MAX, 0x8000000080000000ull,
            0x7FFFFFFF7FFFFFFFull, 0x8000800080008000ull, 0x7FFF7FFF7FFF7FFFull};
        for (auto& value : state.vfp_d) value = values[pattern];
    }
    state.fpscr = static_cast<uint32_t>(rng());
    const uint32_t flags = state.fpscr;
    std::array<uint64_t, 32> expected;
    std::memcpy(expected.data(), state.vfp_d, sizeof(state.vfp_d));
    const uint64_t scalar = (state.vfp_d[m] >> (index * size)) & Mask(size);
    const uint64_t input = state.vfp_d[n];
    const int64_t scalar_signed = static_cast<int64_t>(scalar) -
        ((scalar >> (size - 1u)) ? (int64_t{1} << size) : 0);
    for (uint32_t lane = 0; lane < 64u / size; ++lane) {
        const uint64_t value = (input >> (lane * size)) & Mask(size);
        const int64_t signed_value = static_cast<int64_t>(value) -
            ((value >> (size - 1u)) ? (int64_t{1} << size) : 0);
        const uint64_t product = u ? value * scalar : static_cast<uint64_t>(signed_value * scalar_signed);
        const uint32_t dest = d + (lane * size * 2u) / 64u;
        const uint32_t shift = (lane * size * 2u) % 64u;
        const uint64_t old = (expected[dest] >> shift) & Mask(size * 2u);
        const uint64_t result = op == Scalar::kS2sMull ? product :
            (op == Scalar::kS2sMlsl ? old - product : old + product);
        const uint64_t mask = Mask(size * 2u) << shift;
        expected[dest] = (expected[dest] & ~mask) | ((result << shift) & mask);
    }
    rig.Engine().Get<Scalar>().HandleScalarMlsMlaLong(op, d, n, m, size, index, u);
    if (std::memcmp(expected.data(), state.vfp_d, sizeof(state.vfp_d)) || state.fpscr != flags)
        TestFail("NEON long scalar mismatch op=%u size=%u u=%u d=%u n=%u m=%u index=%u pattern=%u",
                 op, size, u, d, n, m, index, pattern);
}

void Verify() {
    ArmIrRig rig(false, {});
    std::mt19937_64 rng(0x4E454F4E4C4F4E47ull);
    constexpr std::array<std::array<uint32_t, 3>, 7> aliases = {{{4, 0, 1}, {4, 4, 1},
        {4, 5, 1}, {4, 0, 4}, {4, 0, 5}, {4, 5, 4}, {30, 31, 7}}};
    for (uint32_t size : {16u, 32u})
        for (uint32_t u : {0u, 1u})
            for (uint32_t op : kOps)
                for (const auto& a : aliases)
                    for (uint32_t index = 0; index < 64u / size; ++index)
                        for (uint32_t pattern = 0; pattern < 128u; ++pattern)
                            Check(rig, rng, op, size, u, a[0], a[1], a[2], index, pattern);
}

void Benchmark() {
    ArmIrRig rig(false, {});
    auto& scalar = rig.Engine().Get<Scalar>();
    auto& state = *rig.Cpu().State();
    constexpr uint32_t count = 1000000;
    for (uint32_t size : {16u, 32u})
        for (uint32_t u : {0u, 1u})
            for (uint32_t op : kOps) {
                state.vfp_d[0] = 0x800012347FFF5678ull;
                state.vfp_d[1] = 0x76543210FEDCBA98ull;
                for (uint32_t i = 0; i < 10000u; ++i)
                    scalar.HandleScalarMlsMlaLong(op, 4, 0, 1, size, 1, u);
                for (uint32_t repeat = 0; repeat < 3u; ++repeat) {
                    const auto start = std::chrono::steady_clock::now();
                    for (uint32_t i = 0; i < count; ++i)
                        scalar.HandleScalarMlsMlaLong(op, 4, 0, 1, size, 1, u);
                    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - start).count();
                    std::printf("%u,%u,%u,%u,%.3f,%llu\n", op, size, u, repeat,
                        static_cast<double>(ns) / count,
                        static_cast<unsigned long long>(state.vfp_d[4]));
                }
            }
}

}

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--benchmark") == 0) Benchmark();
    else Verify();
}
