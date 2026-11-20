#pragma once

// KDBG v1 reader and writer (docs/symbols.md, ADR 0009).
//
// The implementation depends only on the C++ standard library. A host that only
// decodes images links `krc::debug` and nothing else.

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace krc::debug {

struct KdbgSource {
    std::uint32_t id = 0;
    std::string path;
};

struct KdbgLine {
    std::uint32_t address = 0;
    std::uint32_t source = 0;
    std::uint32_t line = 0;
    std::uint32_t column = 0;
};

struct KdbgFunction {
    std::string name;
    std::uint32_t start = 0;
    std::uint32_t end = 0;
    std::uint32_t frame = 0;
    std::string type;
};

struct KdbgSymbol {
    std::string name;
    std::uint32_t address = 0;
    std::uint32_t size = 0;
    std::string type;
};

struct KdbgVariable {
    std::string function;
    std::string name;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    std::string type;
};

struct KdbgMember {
    std::string name;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    std::string type;
};

struct KdbgType {
    std::string name;
    std::uint32_t size = 0;
    std::string kind;
    std::vector<KdbgMember> members;
};

struct KdbgImage {
    std::uint32_t target_id = 1;
    std::uint32_t abi_version = 1;
    std::uint8_t pointer_width = 4;
    std::uint8_t endianness = 1;
    std::uint64_t build_id = 0;
    // The image base the payload's absolute addresses were built for. It is
    // stored little-endian in the header's eight reserved bytes at offset 56.
    // Zero means the writer recorded no base (an image predating the field).
    std::uint64_t image_base = 0;
    std::string compiler = "krc";
    std::vector<KdbgSource> sources;
    std::vector<KdbgLine> lines;
    std::vector<KdbgFunction> functions;
    std::vector<KdbgSymbol> symbols;
    std::vector<KdbgVariable> variables;
    std::vector<KdbgType> types;
};

[[nodiscard]] std::vector<std::uint8_t> encode_kdbg(const KdbgImage& image);
[[nodiscard]] bool decode_kdbg(std::span<const std::uint8_t> data, KdbgImage& image,
                               std::string& error);
[[nodiscard]] std::string dump_kdbg_text(const KdbgImage& image);
[[nodiscard]] std::string dump_kdbg_json(const KdbgImage& image);

// A `.kdbg` payload located inside a container image.
struct KdbgSection {
    // Borrows from the container passed to `find_kdbg_section`.
    std::span<const std::uint8_t> payload;
    // The base address the container loads at: the KR32 header's base field, or
    // the address of the KR16 image's first section.
    std::uint64_t image_base = 0;
    // "k32" or "kex".
    std::string_view container;
};

// Finds the `.kdbg` section in a KR32 `.k32` or KR16 `.kex` image.
[[nodiscard]] bool find_kdbg_section(std::span<const std::uint8_t> container,
                                     KdbgSection& section, std::string& error);

// Finds and decodes the payload. A payload that records an image base different
// from the one the container loads at is rejected, so a debugger cannot place
// breakpoints using addresses from a different load. A payload that records no
// base (zero) is accepted for images written before the field existed.
[[nodiscard]] bool decode_kdbg_image(std::span<const std::uint8_t> container,
                                     KdbgImage& image, std::string& error);

}  // namespace krc::debug
