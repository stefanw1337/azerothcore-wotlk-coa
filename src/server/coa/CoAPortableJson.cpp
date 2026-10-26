#include "CoAPortableJson.h"
#include "StringFormat.h"
#include "utf8.h"
#include <algorithm>
#include <limits>

namespace CoAPortableJson
{
    Value const* Value::Find(std::string_view key) const
    {
        for (auto const& member : Members)
            if (member.first == key)
                return &member.second;
        return nullptr;
    }

    namespace
    {
        class Parser
        {
        public:
            Parser(std::string_view text, Limits const& limits) : _text(text), _limits(limits) { }

            Parsed Run()
            {
                Parsed out;
                SkipSpace();
                if (!ParseValue(out.Root, 0))
                {
                    out.Error = _error;
                    return out;
                }
                SkipSpace();
                if (_pos != _text.size())
                {
                    out.Error = Fail("unexpected data after the document");
                    return out;
                }
                out.Ok = true;
                return out;
            }

        private:
            std::string Fail(std::string message)
            {
                if (_error.empty())
                    _error = Acore::StringFormat("{} at byte {}", message, _pos);
                return _error;
            }

            void SkipSpace()
            {
                while (_pos < _text.size() && (_text[_pos] == ' ' || _text[_pos] == '\t' || _text[_pos] == '\n' || _text[_pos] == '\r'))
                    ++_pos;
            }

            bool Literal(std::string_view word)
            {
                if (_text.substr(_pos, word.size()) != word)
                    return false;
                _pos += word.size();
                return true;
            }

            bool ParseValue(Value& out, uint32 depth)
            {
                if (depth > _limits.MaxDepth)
                    return Fail("the document is nested too deeply"), false;
                if (++_nodes > _limits.MaxNodes)
                    return Fail("the document has too many values"), false;
                if (_pos >= _text.size())
                    return Fail("the document ends too early"), false;
                char const c = _text[_pos];
                if (c == '{')
                    return ParseObject(out, depth);
                if (c == '[')
                    return ParseArray(out, depth);
                if (c == '"')
                {
                    out.Kind = Type::String;
                    return ParseString(out.Text);
                }
                if (c == '-' || (c >= '0' && c <= '9'))
                    return ParseInteger(out);
                if (Literal("true"))
                {
                    out.Kind = Type::Bool;
                    out.Boolean = true;
                    return true;
                }
                if (Literal("false"))
                {
                    out.Kind = Type::Bool;
                    return true;
                }
                if (Literal("null"))
                {
                    out.Kind = Type::Null;
                    return true;
                }
                return Fail("unexpected character"), false;
            }

            bool ParseInteger(Value& out)
            {
                out.Kind = Type::Integer;
                if (_text[_pos] == '-')
                {
                    out.Negative = true;
                    ++_pos;
                }
                std::size_t const start = _pos;
                uint64 value = 0;
                while (_pos < _text.size() && _text[_pos] >= '0' && _text[_pos] <= '9')
                {
                    uint64 const digit = uint64(_text[_pos] - '0');
                    if (value > (std::numeric_limits<uint64>::max() - digit) / 10)
                        return Fail("a number is too large"), false;
                    value = value * 10 + digit;
                    ++_pos;
                }
                if (_pos == start || (_text[start] == '0' && _pos - start > 1))
                    return Fail("a number is malformed"), false;
                if (_pos < _text.size() && (_text[_pos] == '.' || _text[_pos] == 'e' || _text[_pos] == 'E'))
                    return Fail("fractions and exponents are not part of the format"), false;
                out.Magnitude = value;
                return true;
            }

            static void AppendUtf8(std::string& out, uint32 cp)
            {
                utf8::append(char32_t(cp), std::back_inserter(out));
            }

            bool Hex4(uint32& out)
            {
                if (_pos + 4 > _text.size())
                    return false;
                out = 0;
                for (int i = 0; i < 4; ++i)
                {
                    char const c = _text[_pos++];
                    out <<= 4;
                    if (c >= '0' && c <= '9')
                        out |= uint32(c - '0');
                    else if (c >= 'a' && c <= 'f')
                        out |= uint32(c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F')
                        out |= uint32(c - 'A' + 10);
                    else
                        return false;
                }
                return true;
            }

            bool ParseString(std::string& out)
            {
                ++_pos;
                while (true)
                {
                    if (_pos >= _text.size())
                        return Fail("a string is not closed"), false;
                    unsigned char const c = static_cast<unsigned char>(_text[_pos++]);
                    if (c == '"')
                        break;
                    if (c < 0x20)
                        return Fail("a control character inside a string"), false;
                    if (c != '\\')
                    {
                        out.push_back(char(c));
                    }
                    else
                    {
                        if (_pos >= _text.size())
                            return Fail("a string is not closed"), false;
                        char const e = _text[_pos++];
                        switch (e)
                        {
                            case '"': out.push_back('"'); break;
                            case '\\': out.push_back('\\'); break;
                            case '/': out.push_back('/'); break;
                            case 'b': out.push_back('\b'); break;
                            case 'f': out.push_back('\f'); break;
                            case 'n': out.push_back('\n'); break;
                            case 'r': out.push_back('\r'); break;
                            case 't': out.push_back('\t'); break;
                            case 'u':
                            {
                                uint32 cp = 0;
                                if (!Hex4(cp))
                                    return Fail("a bad \\u escape"), false;
                                if (cp >= 0xD800 && cp <= 0xDBFF)
                                {
                                    uint32 low = 0;
                                    if (!Literal("\\u") || !Hex4(low) || low < 0xDC00 || low > 0xDFFF)
                                        return Fail("a lone surrogate"), false;
                                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                                }
                                else if (cp >= 0xDC00 && cp <= 0xDFFF)
                                    return Fail("a lone surrogate"), false;
                                if (cp == 0)
                                    return Fail("a NUL inside a string"), false;
                                AppendUtf8(out, cp);
                                break;
                            }
                            default:
                                return Fail("an unknown escape"), false;
                        }
                    }
                    if (out.size() > _limits.MaxStringBytes)
                        return Fail("a string is too long"), false;
                }
                if (!utf8::is_valid(out.begin(), out.end()))
                    return Fail("a string is not valid UTF-8"), false;
                return true;
            }

            bool ParseArray(Value& out, uint32 depth)
            {
                out.Kind = Type::Array;
                ++_pos;
                SkipSpace();
                if (_pos < _text.size() && _text[_pos] == ']')
                {
                    ++_pos;
                    return true;
                }
                while (true)
                {
                    SkipSpace();
                    Value item;
                    if (!ParseValue(item, depth + 1))
                        return false;
                    out.Items.push_back(std::move(item));
                    SkipSpace();
                    if (_pos >= _text.size())
                        return Fail("an array is not closed"), false;
                    if (_text[_pos] == ',')
                    {
                        ++_pos;
                        continue;
                    }
                    if (_text[_pos] == ']')
                    {
                        ++_pos;
                        return true;
                    }
                    return Fail("expected , or ]"), false;
                }
            }

            bool ParseObject(Value& out, uint32 depth)
            {
                out.Kind = Type::Object;
                ++_pos;
                SkipSpace();
                if (_pos < _text.size() && _text[_pos] == '}')
                {
                    ++_pos;
                    return true;
                }
                while (true)
                {
                    SkipSpace();
                    if (_pos >= _text.size() || _text[_pos] != '"')
                        return Fail("expected a member name"), false;
                    std::string key;
                    if (!ParseString(key))
                        return false;
                    for (auto const& member : out.Members)
                        if (member.first == key)
                            return Fail("a duplicate member name"), false;
                    SkipSpace();
                    if (_pos >= _text.size() || _text[_pos] != ':')
                        return Fail("expected :"), false;
                    ++_pos;
                    SkipSpace();
                    Value item;
                    if (!ParseValue(item, depth + 1))
                        return false;
                    out.Members.emplace_back(std::move(key), std::move(item));
                    SkipSpace();
                    if (_pos >= _text.size())
                        return Fail("an object is not closed"), false;
                    if (_text[_pos] == ',')
                    {
                        ++_pos;
                        continue;
                    }
                    if (_text[_pos] == '}')
                    {
                        ++_pos;
                        return true;
                    }
                    return Fail("expected , or }"), false;
                }
            }

            std::string_view _text;
            Limits const& _limits;
            std::size_t _pos = 0;
            uint32 _nodes = 0;
            std::string _error;
        };
    }

    Parsed Parse(std::string_view text, Limits const& limits)
    {
        return Parser(text, limits).Run();
    }

    Reader::Reader(Value const& object, std::string path) : _object(object), _path(std::move(path))
    {
        if (object.Kind != Type::Object)
            Fail("is not an object");
    }

    Reader Reader::Child(Value const& object, std::string const& path)
    {
        return Reader(object, path);
    }

    void Reader::Fail(std::string message)
    {
        if (_error.empty())
            _error = Acore::StringFormat("{} {}", _path, message);
    }

    Value const* Reader::Take(std::string_view key)
    {
        if (!_error.empty())
            return nullptr;
        Value const* found = _object.Find(key);
        if (!found)
        {
            Fail(Acore::StringFormat("lacks \"{}\"", key));
            return nullptr;
        }
        _taken.push_back(key);
        return found;
    }

    uint64 Reader::U64(std::string_view key, uint64 maximum)
    {
        Value const* v = Take(key);
        if (!v)
            return 0;
        if (v->Kind != Type::Integer || v->Negative || v->Magnitude > maximum)
        {
            Fail(Acore::StringFormat("has a bad \"{}\"", key));
            return 0;
        }
        return v->Magnitude;
    }

    int64 Reader::I64(std::string_view key, int64 minimum, int64 maximum)
    {
        Value const* v = Take(key);
        if (!v)
            return 0;
        if (v->Kind != Type::Integer || v->Magnitude > uint64(std::numeric_limits<int64>::max()))
        {
            Fail(Acore::StringFormat("has a bad \"{}\"", key));
            return 0;
        }
        int64 const value = v->Negative ? -int64(v->Magnitude) : int64(v->Magnitude);
        if (value < minimum || value > maximum)
        {
            Fail(Acore::StringFormat("has a bad \"{}\"", key));
            return 0;
        }
        return value;
    }

    bool Reader::Bool(std::string_view key)
    {
        Value const* v = Take(key);
        if (!v)
            return false;
        if (v->Kind != Type::Bool)
        {
            Fail(Acore::StringFormat("has a bad \"{}\"", key));
            return false;
        }
        return v->Boolean;
    }

    std::string Reader::Str(std::string_view key, std::size_t maxBytes)
    {
        Value const* v = Take(key);
        if (!v)
            return {};
        if (v->Kind != Type::String || v->Text.size() > maxBytes)
        {
            Fail(Acore::StringFormat("has a bad \"{}\"", key));
            return {};
        }
        return v->Text;
    }

    std::string Reader::OptStr(std::string_view key, std::size_t maxBytes, bool& present)
    {
        present = false;
        Value const* v = Take(key);
        if (!v)
            return {};
        if (v->Kind == Type::Null)
            return {};
        if (v->Kind != Type::String || v->Text.size() > maxBytes)
        {
            Fail(Acore::StringFormat("has a bad \"{}\"", key));
            return {};
        }
        present = true;
        return v->Text;
    }

    bool Reader::Nullable(std::string_view key)
    {
        Value const* v = _error.empty() ? _object.Find(key) : nullptr;
        return v && v->Kind == Type::Null;
    }

    Value const* Reader::Array(std::string_view key, std::size_t maxItems)
    {
        Value const* v = Take(key);
        if (!v)
            return nullptr;
        if (v->Kind != Type::Array || v->Items.size() > maxItems)
        {
            Fail(Acore::StringFormat("has a bad \"{}\"", key));
            return nullptr;
        }
        return v;
    }

    Value const* Reader::Object(std::string_view key)
    {
        Value const* v = Take(key);
        if (!v)
            return nullptr;
        if (v->Kind != Type::Object)
        {
            Fail(Acore::StringFormat("has a bad \"{}\"", key));
            return nullptr;
        }
        return v;
    }

    Value const* Reader::Raw(std::string_view key)
    {
        return Take(key);
    }

    void Reader::Finish()
    {
        if (!_error.empty())
            return;
        for (auto const& member : _object.Members)
            if (std::find(_taken.begin(), _taken.end(), std::string_view(member.first)) == _taken.end())
                Fail(Acore::StringFormat("has an unknown field \"{}\"", member.first));
    }
}
