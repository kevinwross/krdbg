#pragma once

// KDBG v1.1 reader and writer (docs/symbols.md, ADR 0009).
//
// The implementation depends only on the C++ standard library. A host that only
// decodes images links `krc::debug` and nothing else.
//
// v1.1 is additive: every v1 record keeps its tag and payload, and the new
// records are new tags, new sections, and new `.debug_info` letters. A v1 reader
// skips them; this reader accepts a v1 payload and leaves the new fields empty.

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
    // v1 stores a u32; a v1.1 wide payload stores the u64 form, and this reader
    // zero-extends the v1 field.
    std::uint64_t address = 0;
    std::uint32_t source = 0;
    std::uint32_t line = 0;
    std::uint32_t column = 0;
};

struct KdbgFunction {
    std::string name;
    std::uint64_t start = 0;
    std::uint64_t end = 0;
    std::uint32_t frame = 0;
    std::string type;
};

struct KdbgSymbol {
    std::string name;
    std::uint64_t address = 0;
    std::uint64_t size = 0;
    std::string type;
};

// A register-held variable over a half-open live range. v1.1 adds it; no
// compiler path produces one until promotion lands (ADR 0052).
struct KdbgLocation {
    std::string function;
    std::string name;
    std::uint16_t reg = 0;
    bool fp = false;
    std::uint64_t start = 0;
    std::uint64_t end = 0;
};

// A lexical scope over a half-open range. `parent` is empty for the function
// scope.
struct KdbgScope {
    std::string function;
    std::string name;
    std::string parent;
    std::uint64_t start = 0;
    std::uint64_t end = 0;
};

enum class KdbgVariableKind : std::uint16_t {
    Parameter = 1,
    Local = 2,
    Static = 3,
};

// Parameter/local/static flags and the scope a variable belongs to. v1.1.
struct KdbgVarInfo {
    std::string function;
    std::string name;
    KdbgVariableKind kind = KdbgVariableKind::Local;
    std::uint16_t flags = 0;
    std::string scope;
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
    // v1.1: the base class name (empty for none), the element type and count of
    // an array, and the target type of a pointer. A type carries at most the
    // fields its kind uses; the dumper prints the ones that are set.
    std::string base;
    std::string element;
    std::uint32_t element_count = 0;
    std::string target;
    std::vector<KdbgMember> members;
};

struct KdbgImage {
    std::uint32_t target_id = 1;
    std::uint32_t abi_version = 1;
    std::uint16_t major_version = 1;
    std::uint16_t minor_version = 1;
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
    std::vector<KdbgVarInfo> variable_info;
    std::vector<KdbgLocation> locations;
    std::vector<KdbgScope> scopes;
    std::vector<KdbgType> types;
};

// The v1.1 facts a caller can require. `require_kdbg_feature` reports "newer
// KDBG" when the payload's minor is newer than this reader, "predates v1.1"
// when the payload is v1, and a plain missing-records error otherwise.
enum class KdbgFeature {
    Locations,
    Scopes,
    VariableInfo,
    BaseTypes,
    ArrayTypes,
    TypeRefs,
    WideAddresses,
};

[[nodiscard]] bool require_kdbg_feature(const KdbgImage& image,
                                        KdbgFeature feature, std::string& error);

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

// Finds the `.kdbg` section in a KR32 `.kr32` or KR16 `.kr16` image.
[[nodiscard]] bool find_kdbg_section(std::span<const std::uint8_t> container,
                                     KdbgSection& section, std::string& error);

// Finds and decodes the payload. A payload that records an image base different
// from the one the container loads at is rejected, so a debugger cannot place
// breakpoints using addresses from a different load. A payload that records no
// base (zero) is accepted for images written before the field existed.
[[nodiscard]] bool decode_kdbg_image(std::span<const std::uint8_t> container,
                                     KdbgImage& image, std::string& error);

}  // namespace krc::debug
