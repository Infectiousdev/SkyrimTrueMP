// Tests for the core library (Code/core): memory, buffers, serialization and the small utilities.

#include <catch2/catch.hpp>

#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Filesystem.hpp>
#include <TiltedCore/Hash.hpp>
#include <TiltedCore/Initializer.hpp>
#include <TiltedCore/Lockable.hpp>
#include <TiltedCore/Math.hpp>
#include <TiltedCore/Meta.hpp>
#include <TiltedCore/Outcome.hpp>
#include <TiltedCore/Platform.hpp>
#include <TiltedCore/ScratchAllocator.hpp>
#include <TiltedCore/Serialization.hpp>
#include <TiltedCore/Signal.hpp>
#include <TiltedCore/StackAllocator.hpp>
#include <TiltedCore/Stl.hpp>
#include <TiltedCore/TaskQueue.hpp>
#include <TiltedCore/ViewBuffer.hpp>

#include <atomic>
#include <cstring>
#include <limits>
#include <random>
#include <thread>

using namespace TiltedPhoques;

namespace
{
// A deterministic source of random numbers, so a failure reproduces.
struct Random
{
    std::mt19937_64 Engine{20260105};
    uint64_t Next() { return Engine(); }
    uint64_t Bits(size_t aCount) { return aCount == 0 ? 0 : (aCount == 64 ? Next() : Next() & ((uint64_t(1) << aCount) - 1)); }
};

bool Inside(const void* apBlock, const void* apStart, size_t aSize)
{
    const auto* p = static_cast<const unsigned char*>(apBlock);
    const auto* s = static_cast<const unsigned char*>(apStart);
    return p >= s && p < s + aSize;
}
} // namespace

// ----------------------------------------------------------------------------------- allocators

TEST_CASE("Tagged allocations are aligned and freeable", "[core.alloc]")
{
    for (size_t alignment : {size_t(1), size_t(8), size_t(16), size_t(32), size_t(64), size_t(4096)})
    {
        for (size_t size : {size_t(1), size_t(17), size_t(1000)})
        {
            void* pBlock = Allocator::AllocateTagged(size, alignment);
            REQUIRE(pBlock != nullptr);
            REQUIRE(reinterpret_cast<uintptr_t>(pBlock) % std::max<size_t>(alignment, 16) == 0);
            std::memset(pBlock, 0xAB, size); // the whole block is usable
            Allocator::FreeTagged(pBlock);
        }
    }

    Allocator::FreeTagged(nullptr); // freeing nothing is fine
}

TEST_CASE("A scoped allocator serves this thread, and restores the previous one", "[core.alloc]")
{
    Allocator* pOriginal = Allocator::Get();
    REQUIRE(pOriginal == Allocator::GetDefault());

    StackAllocator<1024> outer;
    {
        ScopedAllocator outerScope{outer};
        REQUIRE(Allocator::Get() == &outer);

        StackAllocator<1024> inner;
        {
            ScopedAllocator innerScope{inner};
            REQUIRE(Allocator::Get() == &inner);
        }
        REQUIRE(Allocator::Get() == &outer);
    }
    REQUIRE(Allocator::Get() == pOriginal);

    SECTION("it does not leak into other threads")
    {
        ScopedAllocator scope{outer};
        Allocator* pSeenElsewhere = nullptr;
        std::thread([&] { pSeenElsewhere = Allocator::Get(); }).join();
        REQUIRE(pSeenElsewhere == Allocator::GetDefault());
    }
}

TEST_CASE("Scratch and stack allocators bump, reset and run out cleanly", "[core.alloc]")
{
    StackAllocator<256> stack;

    void* a = stack.Allocate(10);
    void* b = stack.Allocate(10);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(b != a);
    REQUIRE(reinterpret_cast<uintptr_t>(b) % 16 == 0); // each block keeps 16-byte alignment

    REQUIRE(stack.Allocate(1000) == nullptr); // too big
    stack.Free(a);                            // does nothing, and does not crash

    stack.Reset();
    REQUIRE(stack.Used() == 0);
    REQUIRE(stack.Allocate(10) == a); // everything is available again

    ScratchAllocator scratch(512);
    REQUIRE(scratch.Capacity() == 512);
    REQUIRE(scratch.Allocate(512) != nullptr);
    REQUIRE(scratch.Allocate(1) == nullptr);
    scratch.Reset();
    REQUIRE(scratch.Allocate(512) != nullptr);
}

TEST_CASE("When a scratch allocator is full, tagged allocation falls back to the heap", "[core.alloc]")
{
    StackAllocator<64> tiny;
    ScopedAllocator scope{tiny};

    void* pSmall = Allocator::AllocateTagged(8);
    REQUIRE(Inside(pSmall, &tiny, sizeof(tiny))); // served by the stack allocator

    void* pBig = Allocator::AllocateTagged(10'000);
    REQUIRE(pBig != nullptr);
    REQUIRE_FALSE(Inside(pBig, &tiny, sizeof(tiny))); // spilled to the default allocator

    // Both go back to wherever they came from, even though the stack allocator is current.
    Allocator::FreeTagged(pBig);
    Allocator::FreeTagged(pSmall);
}

TEST_CASE("Memory outlives the scope that allocated it", "[core.alloc]")
{
    // The reason allocations are tagged: a container made while a scratch allocator was current can
    // be destroyed after the scope, and must hand its memory back to the right place.
    StackAllocator<4096> stack;
    Vector<int>* pVector = nullptr;
    {
        ScopedAllocator scope{stack};
        pVector = new Vector<int>();
        // (the Vector object itself is on the heap; its element storage comes from `stack`)
        for (int i = 0; i < 100; ++i)
            pVector->push_back(i);
        REQUIRE(Inside(pVector->data(), &stack, sizeof(stack)));
    }

    REQUIRE(pVector->size() == 100);
    REQUIRE((*pVector)[99] == 99);
    delete pVector; // outside the scope: frees to the stack allocator, a no-op, not to the heap
}

TEST_CASE("AllocatorCompatible types allocate from the current allocator", "[core.alloc]")
{
    struct Message : AllocatorCompatible
    {
        virtual ~Message() = default;
        int Value = 7;
    };
    struct Special : Message
    {
        char Padding[100];
    };

    StackAllocator<1024> stack;
    Message* pMessage;
    {
        ScopedAllocator scope{stack};
        pMessage = new Special();
        REQUIRE(Inside(pMessage, &stack, sizeof(stack)));
        REQUIRE(pMessage->Value == 7);

        auto pUnique = MakeUnique<Message>();
        REQUIRE(Inside(pUnique.get(), &stack, sizeof(stack)));
    }
    delete pMessage; // through the base pointer, outside the scope

    // Created normally, it comes from the default allocator.
    auto pPlain = MakeUnique<Message>();
    REQUIRE(pPlain->Value == 7);
}

TEST_CASE("Containers", "[core.alloc]")
{
    SECTION("strings compare and convert with the standard ones")
    {
        String text = "hello";
        text += " world";
        REQUIRE(text == "hello world");
        REQUIRE(std::string(text.c_str()) == "hello world");
        REQUIRE(String("x") != String("y"));
    }

    SECTION("maps and sets work, including with string keys")
    {
        Map<String, int> map;
        map["one"] = 1;
        map["two"] = 2;
        REQUIRE(map.at("two") == 2);
        REQUIRE(map.find("three") == map.end());

        Set<int> set{1, 2, 3, 2};
        REQUIRE(set.size() == 3);

        SortedMap<int, String> sorted;
        sorted[3] = "c";
        sorted[1] = "a";
        REQUIRE(sorted.begin()->first == 1);
    }

    SECTION("vectors of different allocator scopes can be moved and swapped")
    {
        StackAllocator<4096> stack;
        Vector<int> fromStack;
        {
            ScopedAllocator scope{stack};
            fromStack.assign({1, 2, 3});
        }
        Vector<int> fromHeap{9, 8};

        fromHeap = std::move(fromStack);
        REQUIRE(fromHeap == (Vector<int>{1, 2, 3}));
        Vector<int> other{4};
        other.swap(fromHeap);
        REQUIRE(other == (Vector<int>{1, 2, 3}));
    }

    SECTION("CastUnique moves ownership to a derived type")
    {
        struct Base
        {
            virtual ~Base() = default;
        };
        struct Derived : Base
        {
            int Tag = 5;
        };
        UniquePtr<Base> base = MakeUnique<Derived>();
        auto derived = CastUnique<Derived>(std::move(base));
        REQUIRE(base == nullptr);
        REQUIRE(derived->Tag == 5);
    }
}

TEST_CASE("Class boilerplate macros", "[core.meta]")
{
    struct Pinned
    {
        TP_NOCOPYMOVE(Pinned);
        Pinned() = default;
    };
    static_assert(!std::is_copy_constructible_v<Pinned>);
    static_assert(!std::is_move_constructible_v<Pinned>);
    static_assert(!std::is_copy_assignable_v<Pinned>);
    static_assert(!std::is_move_assignable_v<Pinned>);

    struct Movable
    {
        TP_NOCOPY(Movable);
        Movable() = default;
        Movable(Movable&&) = default;
    };
    static_assert(!std::is_copy_constructible_v<Movable>);
    static_assert(std::is_move_constructible_v<Movable>);

    // TP_UNUSED can discard a [[nodiscard]] result on purpose.
    []() -> int [[nodiscard]] { return 3; }();
    SUCCEED();
}

// ---------------------------------------------------------------------------------- bit buffer

TEST_CASE("Bits are packed least significant first", "[core.buffer]")
{
    // This layout is the wire format; the test pins it.
    Buffer buffer(4);
    Buffer::Writer writer(&buffer);
    REQUIRE(writer.WriteBits(1, 1));      // bit 0
    REQUIRE(writer.WriteBits(0b101, 3));  // bits 1-3
    REQUIRE(writer.WriteBits(0xF, 4));    // bits 4-7
    REQUIRE(writer.WriteBits(0x1234, 16)); // the next two bytes
    REQUIRE(buffer[0] == 0xFB);            // 1111 1011
    REQUIRE(buffer[1] == 0x34);            // low byte first
    REQUIRE(buffer[2] == 0x12);
    REQUIRE(writer.Size() == 3);
    REQUIRE(writer.GetBitPosition() == 24);
}

TEST_CASE("Any width at any alignment round trips", "[core.buffer]")
{
    Random random;
    for (int trial = 0; trial < 300; ++trial)
    {
        Buffer buffer(512);
        Buffer::Writer writer(&buffer);

        std::vector<std::pair<size_t, uint64_t>> written;
        while (true)
        {
            const size_t width = random.Next() % 65; // 0..64
            const uint64_t value = random.Bits(width);
            if (!writer.WriteBits(value, width))
                break;
            written.emplace_back(width, value);
        }

        Buffer::Reader reader(&buffer);
        for (const auto& [width, value] : written)
        {
            uint64_t read = 12345;
            REQUIRE(reader.ReadBits(read, width));
            REQUIRE(read == value);
        }
    }
}

TEST_CASE("Writing more than a value's width keeps only the low bits", "[core.buffer]")
{
    Buffer buffer(8);
    Buffer::Writer writer(&buffer);
    REQUIRE(writer.WriteBits(0xFFFF, 4));
    REQUIRE(writer.WriteBits(0, 4));
    REQUIRE(buffer[0] == 0x0F); // the upper bits of 0xFFFF did not spill into the next field
}

TEST_CASE("Reads and writes never leave the buffer", "[core.buffer]")
{
    Buffer buffer(2); // 16 bits
    Buffer::Writer writer(&buffer);

    REQUIRE(writer.WriteBits(0x3FFF, 14));
    REQUIRE_FALSE(writer.WriteBits(0xF, 3)); // would need 17 bits
    REQUIRE(writer.GetBitPosition() == 14);  // a failed write consumes nothing
    REQUIRE(writer.WriteBits(0x3, 2));       // exactly fills it
    REQUIRE_FALSE(writer.WriteBits(1, 1));
    REQUIRE(writer.Eof());
    REQUIRE_FALSE(writer.WriteBits(0, 65)); // wider than a 64-bit value

    Buffer::Reader reader(&buffer);
    uint64_t value = 99;
    REQUIRE(reader.ReadBits(value, 16));
    REQUIRE(value == 0xFFFF);
    REQUIRE_FALSE(reader.ReadBits(value, 1));
    REQUIRE(value == 0); // failure leaves a defined value
    REQUIRE_FALSE(reader.ReadBits(value, 65));

    // A zero-width field always succeeds.
    Buffer::Reader empty(&buffer);
    REQUIRE(empty.ReadBits(value, 0));
    REQUIRE(value == 0);
}

TEST_CASE("Byte blocks, aligned and not", "[core.buffer]")
{
    Random random;
    std::vector<uint8_t> payload(100);
    for (auto& b : payload)
        b = static_cast<uint8_t>(random.Next());

    for (size_t offset : {size_t(0), size_t(1), size_t(3), size_t(7)})
    {
        Buffer buffer(200);
        Buffer::Writer writer(&buffer);
        REQUIRE(writer.WriteBits(0x5, offset)); // misalign
        REQUIRE(writer.WriteBytes(payload.data(), payload.size()));

        Buffer::Reader reader(&buffer);
        uint64_t prefix = 0;
        REQUIRE(reader.ReadBits(prefix, offset));
        std::vector<uint8_t> out(payload.size());
        REQUIRE(reader.ReadBytes(out.data(), out.size()));
        REQUIRE(out == payload);
    }

    SECTION("block sizes that do not fit fail without touching anything")
    {
        Buffer buffer(8);
        Buffer::Writer writer(&buffer);
        REQUIRE_FALSE(writer.WriteBytes(payload.data(), 9));
        REQUIRE_FALSE(writer.WriteBytes(payload.data(), std::numeric_limits<size_t>::max()));
        REQUIRE(writer.GetBitPosition() == 0);

        Buffer::Reader reader(&buffer);
        REQUIRE_FALSE(reader.ReadBytes(payload.data(), std::numeric_limits<size_t>::max()));
        REQUIRE_FALSE(reader.ReadBytes(payload.data(), std::numeric_limits<size_t>::max() / 8 + 1));
    }
}

TEST_CASE("Cursors", "[core.buffer]")
{
    Buffer buffer(16);
    Buffer::Reader reader(&buffer);

    REQUIRE_FALSE(reader.Eof());
    reader.Advance(4);
    REQUIRE(reader.GetBytePosition() == 4);
    REQUIRE(reader.GetBitPosition() == 32);
    REQUIRE(reader.GetDataAtPosition() == buffer.GetData() + 4);
    reader.Reverse(1);
    REQUIRE(reader.GetBytePosition() == 3);
    reader.Reverse(100); // clamps at the start
    REQUIRE(reader.GetBitPosition() == 0);

    uint64_t bits;
    reader.ReadBits(bits, 3);
    REQUIRE(reader.Size() == 1); // a partly used byte counts as used
    REQUIRE(reader.GetBytePosition() == 0);
    reader.Reset();
    REQUIRE(reader.GetBitPosition() == 0);

    reader.Advance(16);
    REQUIRE(reader.Eof());
    REQUIRE(reader.GetBuffer() == &buffer);
}

TEST_CASE("Buffers copy, move, resize and view", "[core.buffer]")
{
    SECTION("construction from data copies it")
    {
        const uint8_t source[] = {1, 2, 3};
        Buffer buffer(source, 3);
        REQUIRE(buffer.GetSize() == 3);
        REQUIRE(buffer[1] == 2);
        buffer[1] = 9;
        REQUIRE(source[1] == 2);
    }

    SECTION("a new buffer is zeroed")
    {
        Buffer buffer(64);
        for (size_t i = 0; i < 64; ++i)
            REQUIRE(buffer[i] == 0);
    }

    SECTION("copies are independent, moves take the contents")
    {
        Buffer a(4);
        a[0] = 7;
        Buffer b(a);
        b[0] = 8;
        REQUIRE(a[0] == 7);

        Buffer c(std::move(a));
        REQUIRE(c[0] == 7);
        REQUIRE(a.GetSize() == 0);

        Buffer d;
        d = c;
        d = d; // self-assignment is harmless
        REQUIRE(d[0] == 7);
        d = std::move(c);
        REQUIRE(d[0] == 7);
    }

    SECTION("resize keeps what fits and zeroes the rest")
    {
        Buffer buffer(4);
        for (size_t i = 0; i < 4; ++i)
            buffer[i] = static_cast<uint8_t>(i + 1);

        REQUIRE(buffer.Resize(8));
        REQUIRE(buffer.GetSize() == 8);
        REQUIRE(buffer[3] == 4);
        REQUIRE(buffer[4] == 0);

        REQUIRE(buffer.Resize(2));
        REQUIRE(buffer[1] == 2);
        REQUIRE(buffer.Resize(0));
        REQUIRE(buffer.GetSize() == 0);
    }

    SECTION("a view reads and writes someone else's memory, and never frees it")
    {
        uint8_t storage[8] = {};
        {
            ViewBuffer view(storage, sizeof(storage));
            Buffer::Writer writer(&view);
            writer.WriteBits(0xAB, 8);
            REQUIRE(view.GetSize() == 8);
        }
        REQUIRE(storage[0] == 0xAB); // the write went to the original, which is still valid
    }

    SECTION("a buffer made under a scratch allocator can outlive it")
    {
        StackAllocator<256> stack;
        Buffer buffer;
        {
            ScopedAllocator scope{stack};
            buffer = Buffer(128);
        }
        buffer[0] = 1; // still usable
        REQUIRE(buffer.GetSize() == 128);
    }
}

// ------------------------------------------------------------------------------ serialization

TEST_CASE("Variable length integers", "[core.serialization]")
{
    const uint64_t values[] = {0, 1, 127, 128, 255, 16383, 16384, 0xFFFFFFFFull, 0x100000000ull, uint64_t(1) << 63, std::numeric_limits<uint64_t>::max()};
    const size_t sizes[] = {1, 1, 1, 2, 2, 2, 3, 5, 5, 10, 10};

    for (size_t i = 0; i < std::size(values); ++i)
    {
        Buffer buffer(16);
        Buffer::Writer writer(&buffer);
        Serialization::WriteVarInt(writer, values[i]);
        REQUIRE(writer.Size() == sizes[i]);

        Buffer::Reader reader(&buffer);
        REQUIRE(Serialization::ReadVarInt(reader) == values[i]);
        REQUIRE(reader.Size() == sizes[i]);
    }
}

TEST_CASE("Malformed variable length integers terminate", "[core.serialization]")
{
    // Continuation bits that never end: reading must stop, not run off or loop.
    Buffer buffer(32);
    std::memset(buffer.GetWriteData(), 0xFF, buffer.GetSize());
    Buffer::Reader reader(&buffer);
    (void)Serialization::ReadVarInt(reader);
    REQUIRE(reader.GetBytePosition() <= 10);

    // A truncated one just ends at the buffer's edge.
    Buffer tiny(1);
    tiny[0] = 0x80;
    Buffer::Reader tinyReader(&tiny);
    REQUIRE(Serialization::ReadVarInt(tinyReader) == 0);
}

TEST_CASE("Strings", "[core.serialization]")
{
    const auto roundTrip = [](const String& aText)
    {
        Buffer buffer(aText.size() + 32);
        Buffer::Writer writer(&buffer);
        Serialization::WriteBool(writer, true); // start unaligned
        Serialization::WriteString(writer, aText);
        Serialization::WriteBool(writer, false);

        Buffer::Reader reader(&buffer);
        REQUIRE(Serialization::ReadBool(reader));
        const String read = Serialization::ReadString(reader);
        REQUIRE_FALSE(Serialization::ReadBool(reader));
        return read;
    };

    REQUIRE(roundTrip("") == "");
    REQUIRE(roundTrip("hello") == "hello");
    REQUIRE(roundTrip("h\xC3\xA9llo \xE2\x82\xAC") == "h\xC3\xA9llo \xE2\x82\xAC"); // UTF-8 passes through
    REQUIRE(roundTrip(String("a\0b", 3)) == String("a\0b", 3));                      // embedded NUL
    REQUIRE(roundTrip(String(100'000, 'x')) == String(100'000, 'x'));
}

TEST_CASE("A string length that exceeds the buffer is refused, not allocated", "[core.serialization]")
{
    Buffer buffer(16);
    Buffer::Writer writer(&buffer);
    Serialization::WriteVarInt(writer, uint64_t(1) << 60); // claims an absurd length
    writer.WriteBytes(reinterpret_cast<const uint8_t*>("abc"), 3);

    Buffer::Reader reader(&buffer);
    const String text = Serialization::ReadString(reader);
    REQUIRE(text.empty());
    REQUIRE(text.capacity() < 100);
}

TEST_CASE("Floating point values keep their exact bits", "[core.serialization]")
{
    const double doubles[] = {0.0, -0.0, 1.5, -2.25e300, std::numeric_limits<double>::denorm_min(), std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()};
    const float floats[] = {0.f, -0.f, 3.14159f, 1e-40f, -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()};

    Buffer buffer(256);
    Buffer::Writer writer(&buffer);
    Serialization::WriteBool(writer, true); // unaligned
    for (double d : doubles)
        Serialization::WriteDouble(writer, d);
    for (float f : floats)
        Serialization::WriteFloat(writer, f);

    Buffer::Reader reader(&buffer);
    REQUIRE(Serialization::ReadBool(reader));
    for (double d : doubles)
    {
        const double read = Serialization::ReadDouble(reader);
        REQUIRE(std::memcmp(&read, &d, sizeof(d)) == 0);
    }
    for (float f : floats)
    {
        const float read = Serialization::ReadFloat(reader);
        REQUIRE(std::memcmp(&read, &f, sizeof(f)) == 0);
    }
}

TEST_CASE("Reading past the end yields zeros, never garbage", "[core.serialization]")
{
    Buffer buffer(1);
    Buffer::Reader reader(&buffer);
    uint64_t sink;
    reader.ReadBits(sink, 8);

    REQUIRE_FALSE(Serialization::ReadBool(reader));
    REQUIRE(Serialization::ReadVarInt(reader) == 0);
    REQUIRE(Serialization::ReadString(reader).empty());
    REQUIRE(Serialization::ReadDouble(reader) == 0.0);
    REQUIRE(Serialization::ReadFloat(reader) == 0.f);
}

// ---------------------------------------------------------------------------------------- hash

TEST_CASE("CRC-64 matches the variant the game data was built with", "[core.hash]")
{
    const auto crc = [](const std::string& aText) { return FHash::Crc64(reinterpret_cast<const unsigned char*>(aText.data()), aText.size()); };

    // The published check value for CRC-64/WE.
    REQUIRE(crc("123456789") == 0x62EC59E3F1A4F00Aull);
    REQUIRE(crc("") == 0);

    // Ground truth from the game's own data: AnimationGraphDescriptor_Chicken.cpp lists a graph's
    // variable names and the key the game looks it up by. The key is this hash of the names,
    // lower-cased and joined. If this fails, every animation graph descriptor stops matching.
    const std::string chickenVariables =
    "blenddefaultblendfastblendslowdirectionisblockingspeedstaggermagnitudeturndeltaisattackreadyweaponsp"
    "eedmultfootikenableballowrotationturnspeedmulticombatstanceisyncturnstateintdirectionbmotiondrivenis"
    "yncidlelocomotionfminturndeltabanimationdrivenboolvariable00isattackingilefthandtypeiweapcategorybwa"
    "ntcastleftbmlh_readyistate_chicken_default_mtistateisbashingisstaggeringisrecoilingspeeddampedspeeds"
    "ampledbheadtrackingtargetlocationbheadtrackingoffslowblendbnoheadtrackaggrowarningblendstaggerdirect"
    "ionicurrentstateidigetuptypeiturnmirroredec_isyncidlelocomotion_1isidlesittingisidlelaybforceidlesto"
    "p";
    REQUIRE(chickenVariables.size() == 601);
    REQUIRE(crc(chickenVariables) == 5224687413749858422ull);
}

// ---------------------------------------------------------------------------------------- math

TEST_CASE("Math helpers", "[core.math]")
{
    SECTION("Mod keeps the sign of the dividend, and accepts mixed types")
    {
        REQUIRE(Mod(7.f, 3.f) == Approx(1.f));
        REQUIRE(Mod(-7.f, 3.f) == Approx(-1.f));
        REQUIRE(Mod(25.0f, 24.f) == Approx(1.f));
        REQUIRE(Mod(7, 2.5) == Approx(2.0));
        static_assert(std::is_same_v<decltype(Mod(1.f, 2.f)), float>);
        static_assert(std::is_same_v<decltype(Mod(1.f, 2.0)), double>);
    }

    SECTION("Lerp works on numbers and on vector-like types")
    {
        REQUIRE(Lerp(0.f, 10.f, 0.25f) == Approx(2.5f));
        REQUIRE(Lerp(10.f, 20.f, 0.f) == Approx(10.f));
        REQUIRE(Lerp(10.f, 20.f, 1.f) == Approx(20.f));

        struct Vec
        {
            float X, Y;
            Vec operator+(const Vec& o) const { return {X + o.X, Y + o.Y}; }
            Vec operator-(const Vec& o) const { return {X - o.X, Y - o.Y}; }
            Vec operator*(float f) const { return {X * f, Y * f}; }
        };
        const Vec mid = Lerp(Vec{0, 10}, Vec{10, 20}, 0.5f);
        REQUIRE(mid.X == Approx(5.f));
        REQUIRE(mid.Y == Approx(15.f));
    }

    SECTION("DeltaAngle takes the short way round")
    {
        const float pi = static_cast<float>(Pi);
        REQUIRE(DeltaAngle(0.f, 1.f, true) == Approx(1.f));
        REQUIRE(DeltaAngle(1.f, 0.f, true) == Approx(-1.f));
        // 350 to 10 degrees is +20, not -340.
        REQUIRE(DeltaAngle(350.f, 10.f, false) == Approx(20.f));
        REQUIRE(DeltaAngle(10.f, 350.f, false) == Approx(-20.f));
        // Across the 2*pi seam in radians.
        REQUIRE(DeltaAngle(2 * pi - 0.1f, 0.1f, true) == Approx(0.2f).margin(1e-4));
        REQUIRE(DeltaAngle(0.1f, 2 * pi - 0.1f, true) == Approx(-0.2f).margin(1e-4));
        // A half turn is reported as +half, never -half.
        REQUIRE(DeltaAngle(0.f, 180.f, false) == Approx(180.f));
        REQUIRE(DeltaAngle(180.f, 0.f, false) == Approx(180.f));
        // Always within range.
        for (float a = -10.f; a < 10.f; a += 0.37f)
            for (float b = -10.f; b < 10.f; b += 0.41f)
            {
                const float d = DeltaAngle(a, b, true);
                REQUIRE(d > -pi - 1e-4f);
                REQUIRE(d <= pi + 1e-4f);
            }
    }

    SECTION("Pi")
    {
        REQUIRE(Pi == Approx(3.14159265358979));
    }
}

// ------------------------------------------------------------------------------------ utilities

TEST_CASE("Outcome holds a value or an error", "[core.outcome]")
{
    const auto find = [](bool aFound) -> Outcome<std::string, bool>
    {
        if (!aFound)
            return false;
        return std::string("payload");
    };

    auto found = find(true);
    REQUIRE(found);
    REQUIRE(found.HasResult());
    REQUIRE_FALSE(found.HasError());
    REQUIRE(found.GetResult() == "payload");
    const std::string moved = found.MoveResult();
    REQUIRE(moved == "payload");

    auto missing = find(false);
    REQUIRE_FALSE(missing);
    REQUIRE(missing.HasError());
    REQUIRE(missing.GetError() == false);

    // Move-only values work.
    const auto makeUnique = []() -> Outcome<std::unique_ptr<int>, bool> { return std::make_unique<int>(4); };
    auto owned = makeUnique();
    REQUIRE(*owned.MoveResult() == 4);
}

TEST_CASE("Initializers run once, in order, when asked", "[core.initializer]")
{
    std::vector<int> order;
    Initializer first([&] { order.push_back(1); });
    Initializer second([&] { order.push_back(2); });
    Initializer registersAnother(
        [&]
        {
            order.push_back(3);
            // Registered during RunAll: runs on the next one, not in this loop.
            static Initializer* s_pLate = new Initializer([&] { order.push_back(4); });
            (void)s_pLate;
        });

    REQUIRE(order.empty()); // nothing happens until RunAll

    Initializer::RunAll();
    REQUIRE(order == (std::vector<int>{1, 2, 3}));

    Initializer::RunAll();
    REQUIRE(order == (std::vector<int>{1, 2, 3, 4}));

    Initializer::RunAll(); // nothing left
    REQUIRE(order.size() == 4);
}

TEST_CASE("TaskQueue hands work to one thread", "[core.taskqueue]")
{
    SECTION("tasks run in order")
    {
        TaskQueue queue;
        std::vector<int> ran;
        for (int i = 0; i < 5; ++i)
            queue.Add([&ran, i] { ran.push_back(i); });
        REQUIRE(queue.Size() == 5);
        queue.Drain();
        REQUIRE(ran == (std::vector<int>{0, 1, 2, 3, 4}));
        REQUIRE(queue.Size() == 0);
    }

    SECTION("work queued while draining waits for the next drain")
    {
        TaskQueue queue;
        int runs = 0;
        std::function<void()> requeue = [&]
        {
            ++runs;
            queue.Add(requeue);
        };
        queue.Add(requeue);

        queue.Drain();
        REQUIRE(runs == 1); // not an endless loop
        queue.Drain();
        REQUIRE(runs == 2);
    }

    SECTION("many producers, one consumer, nothing lost")
    {
        TaskQueue queue;
        std::atomic<int> total{0};
        std::vector<std::thread> producers;
        for (int t = 0; t < 4; ++t)
            producers.emplace_back([&] {
                for (int i = 0; i < 2000; ++i)
                    queue.Add([&total] { ++total; });
            });

        int drained = 0;
        while (total.load() < 8000)
        {
            queue.Drain();
            ++drained;
            if (drained > 100000)
                break;
        }
        for (auto& producer : producers)
            producer.join();
        queue.Drain();
        REQUIRE(total == 8000);
    }
}

TEST_CASE("Lockable guards its value", "[core.lockable]")
{
    SECTION("only one thread at a time")
    {
        Lockable<int> counter(0);
        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t)
            threads.emplace_back([&] {
                for (int i = 0; i < 5000; ++i)
                {
                    auto locked = counter.Lock();
                    ++locked.Get();
                }
            });
        for (auto& thread : threads)
            thread.join();
        REQUIRE(counter.Lock().Get() == 20000);
    }

    SECTION("a recursive mutex lets the owner lock again, and the value is reachable three ways")
    {
        struct Config
        {
            int Value = 5;
        };
        Lockable<Config, std::recursive_mutex> config;
        auto outer = config.Lock();
        auto inner = config.Lock(); // same thread: fine
        REQUIRE(outer->Value == 5);
        REQUIRE((*inner).Value == 5);
        REQUIRE(outer.Get().Value == 5);
    }
}

TEST_CASE("Signals", "[core.signal]")
{
    Signal<void(int, const std::string&)> signal;
    std::vector<std::string> log;

    const auto a = signal.Connect([&](int n, const std::string& s) { log.push_back("a" + std::to_string(n) + s); });
    signal.Connect([&](int n, const std::string&) { log.push_back("b" + std::to_string(n)); });

    signal.Emit(1, "x");
    signal(2, "y"); // call syntax
    REQUIRE(log == (std::vector<std::string>{"a1x", "b1", "a2y", "b2"}));

    log.clear();
    signal.Disconnect(a);
    signal.Emit(3, "z");
    REQUIRE(log == (std::vector<std::string>{"b3"}));
    REQUIRE(signal.ListenerCount() == 1);

    SECTION("a listener may disconnect itself while emitting")
    {
        Signal<void()> once;
        int calls = 0;
        Signal<void()>::ConnectionId id = 0;
        id = once.Connect([&] {
            ++calls;
            once.Disconnect(id);
        });
        once.Emit();
        once.Emit();
        REQUIRE(calls == 1);
    }

    SECTION("a listener connected during an emit starts from the next one")
    {
        Signal<void()> signal2;
        int late = 0;
        bool added = false;
        signal2.Connect([&] {
            if (!added)
            {
                added = true;
                signal2.Connect([&] { ++late; });
            }
        });
        signal2.Emit();
        REQUIRE(late == 0);
        signal2.Emit();
        REQUIRE(late == 1);
    }
}

TEST_CASE("Files", "[core.filesystem]")
{
    const auto path = std::filesystem::temp_directory_path() / "truemp_core_file_test.bin";

    SECTION("binary data survives, including NUL bytes and a large size")
    {
        String data;
        for (int i = 0; i < 200'000; ++i)
            data.push_back(static_cast<char>(i % 256));

        REQUIRE(SaveFile(path, data));
        REQUIRE(LoadFile(path) == data);

        REQUIRE(SaveFile(path, String("short"))); // replaces, does not append
        REQUIRE(LoadFile(path) == "short");
    }

    SECTION("failures are reported, not thrown")
    {
        std::filesystem::remove(path);
        REQUIRE(LoadFile(path).empty());
        REQUIRE_FALSE(SaveFile("/nonexistent-dir-for-truemp-test/file", String("x")));
    }

    SECTION("GetPath names a real directory")
    {
        REQUIRE(std::filesystem::is_directory(GetPath()));
    }

    std::filesystem::remove(path);
}

TEST_CASE("Platform macros", "[core.platform]")
{
#if defined(_WIN32)
    REQUIRE(TP_PLATFORM_WINDOWS == 1);
#else
    REQUIRE(TP_PLATFORM_WINDOWS == 0);
#endif
    REQUIRE(TP_PLATFORM_64 + TP_PLATFORM_32 == 1);
    REQUIRE(TP_PLATFORM_64 == (sizeof(void*) == 8 ? 1 : 0));
}
