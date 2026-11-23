#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace solar {
namespace tesla {

using Bytes = std::vector<uint8_t>;

/** Just enough protobuf wire format for the few Tesla messages we send and read (no code generator, no heap-heavy library). */
class Writer {
public:
    void varint(int field, uint64_t v) {
        tag(field, 0);
        raw(v);
    }
    void boolean(int field, bool v) { varint(field, v ? 1 : 0); }
    void fixed32(int field, uint32_t v) {
        tag(field, 5);
        for (int i = 0; i < 4; i++) out_.push_back(static_cast<uint8_t>(v >> (8 * i)));
    }
    void bytes(int field, const Bytes& b) {
        tag(field, 2);
        raw(b.size());
        out_.insert(out_.end(), b.begin(), b.end());
    }
    void text(int field, const std::string& s) { bytes(field, Bytes(s.begin(), s.end())); }
    /** A nested message. An empty one is still written (it can mean "this option is chosen"). */
    void message(int field, const Writer& inner) { bytes(field, inner.out_); }
    const Bytes& data() const { return out_; }

private:
    void tag(int field, int wire) { raw((static_cast<uint64_t>(field) << 3) | static_cast<uint64_t>(wire)); }
    void raw(uint64_t v) {
        while (v >= 0x80) {
            out_.push_back(static_cast<uint8_t>(v | 0x80));
            v >>= 7;
        }
        out_.push_back(static_cast<uint8_t>(v));
    }
    Bytes out_;
};

/** One field read from a message. For wire type 2 (bytes, text, nested message) `data` and `len` point into the original buffer. */
struct Field {
    int number = 0;
    int wire = 0;            // 0 varint, 2 length-delimited, 5 fixed32
    uint64_t value = 0;      // varint or fixed32 value
    const uint8_t* data = nullptr;
    size_t len = 0;
    Bytes copy() const { return Bytes(data, data + len); }
    std::string str() const { return std::string(reinterpret_cast<const char*>(data), len); }
};

/** Reads protobuf fields out of bytes: the counterpart of `Writer`. */
class Reader {
public:
    Reader(const uint8_t* p, size_t n) : p_(p), end_(p + n) {}
    explicit Reader(const Bytes& b) : p_(b.data()), end_(b.data() + b.size()) {}

    /** Reads the next field. False at the end, or when the bytes are damaged (then ok() is false). */
    bool next(Field& f) {
        if (!ok_ || p_ >= end_) return false;
        uint64_t key;
        if (!varint(key)) return fail();
        f.number = static_cast<int>(key >> 3);
        f.wire = static_cast<int>(key & 7);
        f.data = nullptr;
        f.len = 0;
        f.value = 0;
        switch (f.wire) {
            case 0:
                if (!varint(f.value)) return fail();
                return true;
            case 2: {
                uint64_t n;
                if (!varint(n) || n > static_cast<uint64_t>(end_ - p_)) return fail();
                f.data = p_;
                f.len = static_cast<size_t>(n);
                p_ += n;
                return true;
            }
            case 5:
                if (end_ - p_ < 4) return fail();
                for (int i = 0; i < 4; i++) f.value |= static_cast<uint64_t>(p_[i]) << (8 * i);
                p_ += 4;
                return true;
            case 1:
                if (end_ - p_ < 8) return fail();
                p_ += 8;
                return true;
            default:
                return fail();
        }
    }
    bool ok() const { return ok_; }

private:
    bool fail() {
        ok_ = false;
        return false;
    }
    bool varint(uint64_t& v) {
        v = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (p_ >= end_) return false;
            uint8_t b = *p_++;
            v |= static_cast<uint64_t>(b & 0x7f) << shift;
            if (!(b & 0x80)) return true;
        }
        return false;
    }
    const uint8_t* p_;
    const uint8_t* end_;
    bool ok_ = true;
};

inline Bytes fromHex(const std::string& hex) {
    Bytes out;
    auto nibble = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        int hi = nibble(hex[i]), lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) return Bytes();
        out.push_back(static_cast<uint8_t>(hi * 16 + lo));
    }
    return out;
}

inline std::string toHex(const Bytes& b) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (uint8_t x : b) {
        s += d[x >> 4];
        s += d[x & 15];
    }
    return s;
}

inline Bytes toBytes(const std::string& s) { return Bytes(s.begin(), s.end()); }

}  // namespace tesla
}  // namespace solar
