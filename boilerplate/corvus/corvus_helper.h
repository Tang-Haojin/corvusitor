#ifndef CORVUS_HELPER_H
#define CORVUS_HELPER_H

#include <algorithm>
#include <cstdint>
#include <vector>

// Shared helpers for generated corvus code.
namespace corvus_helper {

inline uint64_t mask_bits(int bits) {
  if (bits <= 0) return 0ULL;
  if (bits >= 64) return ~0ULL;
  return (1ULL << bits) - 1ULL;
}

inline uint64_t pack_payload(uint32_t slotId, uint64_t data, uint32_t chunkIdx,
                             int slotBits, int dataBits, int chunkBits) {
  uint64_t payload = static_cast<uint64_t>(slotId & mask_bits(slotBits));
  payload |= (data & mask_bits(dataBits)) << slotBits;
  if (chunkBits > 0) {
    payload |= (static_cast<uint64_t>(chunkIdx) & mask_bits(chunkBits)) << (slotBits + dataBits);
  }
  return payload;
}

inline uint64_t read_wide_chunk(const uint32_t* words, int wordCount, int dataBits, int chunkIdx) {
  int startBit = chunkIdx * dataBits;
  int remaining = dataBits;
  uint64_t result = 0;
  int outShift = 0;
  while (remaining > 0) {
    int wordIdx = startBit / 32;
    int bitOffset = startBit % 32;
    uint32_t word = (wordIdx < wordCount) ? words[wordIdx] : 0u;
    int take = std::min(remaining, 32 - bitOffset);
    uint32_t mask = (take == 32) ? 0xffffffffu : ((1u << take) - 1u);
    uint64_t chunk = (word >> bitOffset) & mask;
    result |= (chunk << outShift);
    startBit += take;
    remaining -= take;
    outShift += take;
  }
  return result;
}

inline void write_wide_chunk(uint32_t* words, int wordCount, int dataBits, int chunkIdx, uint64_t data) {
  int startBit = chunkIdx * dataBits;
  int remaining = dataBits;
  int inShift = 0;
  while (remaining > 0) {
    int wordIdx = startBit / 32;
    int bitOffset = startBit % 32;
    if (wordIdx >= wordCount) break;
    int take = std::min(remaining, 32 - bitOffset);
    uint32_t mask = (take == 32) ? 0xffffffffu : ((1u << take) - 1u);
    uint32_t fragment = static_cast<uint32_t>((data >> inShift) & mask);
    uint32_t current = words[wordIdx];
    current &= ~(mask << bitOffset);
    current |= (fragment << bitOffset);
    words[wordIdx] = current;
    startBit += take;
    remaining -= take;
    inShift += take;
  }
}

struct SlotDecoder {
  uint32_t slotId;
  uint8_t slotBits;
  uint8_t dataBits;
  uint8_t chunkBits;
  uint32_t chunkCount;
  std::vector<uint64_t> chunks;
  std::vector<bool> filled;
  uint64_t slotMask;
  uint64_t dataMask;
  SlotDecoder(uint32_t id, uint8_t sBits, uint8_t dBits, uint8_t cBits, uint32_t cCount)
      : slotId(id), slotBits(sBits), dataBits(dBits), chunkBits(cBits), chunkCount(cCount),
        chunks(cCount, 0), filled(cCount, false),
        slotMask(mask_bits(sBits)), dataMask(mask_bits(dBits)) {}
  bool consume(uint64_t payload) {
    if ((payload & slotMask) != slotId) return false;
    uint32_t chunkIdx = chunkBits ? static_cast<uint32_t>((payload >> (slotBits + dataBits)) & mask_bits(chunkBits)) : 0;
    if (chunkIdx >= chunkCount) return true;
    uint64_t data = (payload >> slotBits) & dataMask;
    chunks[chunkIdx] = data;
    filled[chunkIdx] = true;
    return true;
  }
  bool complete() const {
    for (bool f : filled) if (!f) return false;
    return true;
  }
};

inline uint64_t assemble_scalar(const SlotDecoder& dec) {
  uint64_t value = 0;
  for (size_t i = 0; i < dec.chunks.size(); ++i) {
    value |= (dec.chunks[i]) << (i * dec.dataBits);
  }
  return value;
}

inline void apply_to_wide(const SlotDecoder& dec, uint32_t* words, int wordCount) {
  std::fill(words, words + wordCount, 0u);
  for (size_t i = 0; i < dec.chunks.size(); ++i) {
    write_wide_chunk(words, wordCount, dec.dataBits, static_cast<int>(i), dec.chunks[i]);
  }
}

} // namespace corvus_helper

#endif // CORVUS_HELPER_H
