#ifndef COA_PORTABLE_JSON_H
#define COA_PORTABLE_JSON_H

#include "Define.h"
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CoAPortableJson
{
    enum class Type : uint8
    {
        Null,
        Bool,
        Integer,
        String,
        Array,
        Object
    };

    struct Value
    {
        Type Kind = Type::Null;
        bool Boolean = false;
        bool Negative = false;
        uint64 Magnitude = 0;
        std::string Text;
        std::vector<Value> Items;
        std::vector<std::pair<std::string, Value>> Members;

        Value const* Find(std::string_view key) const;
    };

    struct Limits
    {
        uint32 MaxDepth = 32;
        uint32 MaxNodes = 4000000;
        uint32 MaxStringBytes = 20 * 1024 * 1024;
    };

    struct Parsed
    {
        bool Ok = false;
        std::string Error;
        Value Root;
    };

    Parsed Parse(std::string_view text, Limits const& limits = Limits());

    class Reader
    {
    public:
        Reader(Value const& object, std::string path);

        bool Valid() const { return _error.empty(); }
        std::string const& Error() const { return _error; }

        uint64 U64(std::string_view key, uint64 maximum);
        int64 I64(std::string_view key, int64 minimum, int64 maximum);
        bool Bool(std::string_view key);
        std::string Str(std::string_view key, std::size_t maxBytes);
        bool Nullable(std::string_view key);
        std::string OptStr(std::string_view key, std::size_t maxBytes, bool& present);
        Value const* Array(std::string_view key, std::size_t maxItems);
        Value const* Object(std::string_view key);
        Value const* Raw(std::string_view key);
        void Finish();

        static Reader Child(Value const& object, std::string const& path);
        void Fail(std::string message);

    private:
        Value const* Take(std::string_view key);

        Value const& _object;
        std::string _path;
        std::string _error;
        std::vector<std::string_view> _taken;
    };
}

#endif
