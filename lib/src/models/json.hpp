#pragma once
// Small strict JSON reader for bounded, local model manifests. No runtime deps.
#include <charconv>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
namespace abc::detail {
struct Json {
  enum Kind { object, array, string, number, boolean, null } kind = null;
  std::map<std::string, Json> members;
  std::vector<Json> items;
  std::string text;
  double numeric = 0;
  bool flag = false;
  const Json &at(const std::string &key) const {
    if (kind != object || !members.contains(key))
      throw std::runtime_error("missing manifest field: " + key);
    return members.at(key);
  }
  bool str(std::string_view s) const { return kind == string && text == s; }
  bool num(double n) const { return kind == number && numeric == n; }
};
class JsonReader {
  std::string_view source;
  std::size_t pos = 0;
  [[noreturn]] void fail() const {
    throw std::runtime_error("invalid model manifest JSON");
  }
  void space() {
    while (pos < source.size() && (source[pos] == ' ' || source[pos] == '\n' ||
                                   source[pos] == '\r' || source[pos] == '\t'))
      ++pos;
  }
  char take() {
    if (pos == source.size())
      fail();
    return source[pos++];
  }
  std::string string() {
    if (take() != '"')
      fail();
    std::string out;
    for (;;) {
      char c = take();
      if (c == '"')
        return out;
      if (static_cast<unsigned char>(c) < 32)
        fail();
      if (c == '\\') {
        char e = take();
        switch (e) {
        case '"':
        case '\\':
        case '/':
          c = e;
          break;
        case 'b':
          c = '\b';
          break;
        case 'f':
          c = '\f';
          break;
        case 'n':
          c = '\n';
          break;
        case 'r':
          c = '\r';
          break;
        case 't':
          c = '\t';
          break;
        case 'u': {
          auto read_hex = [&]() { unsigned code = 0;
          for (int i = 0; i < 4; ++i) {
            char h = take();
            unsigned v = h >= '0' && h <= '9'   ? h - '0'
                         : h >= 'a' && h <= 'f' ? h - 'a' + 10
                         : h >= 'A' && h <= 'F' ? h - 'A' + 10
                                                : 16;
            if (v > 15)
              fail();
            code = code * 16 + v;
          }
          return code; };
          unsigned code = read_hex();
          if (code >= 0xd800 && code <= 0xdbff) {
            if(take()!='\\' || take()!='u')fail();
            unsigned low = read_hex();
            if(low<0xdc00 || low>0xdfff)fail();
            code=0x10000+(code-0xd800)*1024+(low-0xdc00);
          } else if (code >= 0xdc00 && code <= 0xdfff)
            fail();
          if (code < 128)
            out += char(code);
          else if (code < 2048) {
            out += char(0xc0 | (code >> 6));
            out += char(0x80 | (code & 63));
          } else if(code<65536) {
            out += char(0xe0 | (code >> 12));
            out += char(0x80 | ((code >> 6) & 63));
            out += char(0x80 | (code & 63));
          } else {
            out += char(0xf0 | (code >> 18));
            out += char(0x80 | ((code >> 12) & 63));
            out += char(0x80 | ((code >> 6) & 63));
            out += char(0x80 | (code & 63));
          }
          continue;
        }
        default:
          fail();
        }
      }
      out += c;
    }
  }
  Json value(int depth) {
    if (depth > 32)
      fail();
    space();
    if (pos == source.size())
      fail();
    Json v;
    char c = source[pos];
    if (c == '{') {
      v.kind = Json::object;
      ++pos;
      space();
      if (pos < source.size() && source[pos] == '}') {
        ++pos;
        return v;
      }
      for (;;) {
        space();
        auto key = string();
        space();
        if (take() != ':')
          fail();
        auto item = value(depth + 1);
        if (!v.members.emplace(key, std::move(item)).second)
          fail();
        space();
        c = take();
        if (c == '}')
          break;
        if (c != ',')
          fail();
      }
      return v;
    }
    if (c == '[') {
      v.kind = Json::array;
      ++pos;
      space();
      if (pos < source.size() && source[pos] == ']') {
        ++pos;
        return v;
      }
      for (;;) {
        v.items.push_back(value(depth + 1));
        space();
        c = take();
        if (c == ']')
          break;
        if (c != ',')
          fail();
      }
      return v;
    }
    if (c == '"') {
      v.kind = Json::string;
      v.text = string();
      return v;
    }
    for (auto literal : {std::string_view("true"), std::string_view("false"),
                         std::string_view("null")})
      if (source.substr(pos, literal.size()) == literal) {
        pos += literal.size();
        v.kind = literal == "null" ? Json::null : Json::boolean;
        v.flag = literal == "true";
        return v;
      }
    v.kind = Json::number;
    auto begin = pos;
    if (c == '-')
      ++pos;
    if (pos == source.size())
      fail();
    if (source[pos] == '0')
      ++pos;
    else {
      if (source[pos] < '1' || source[pos] > '9')
        fail();
      while (pos < source.size() && source[pos] >= '0' && source[pos] <= '9')
        ++pos;
    }
    if (pos < source.size() && source[pos] == '.') {
      ++pos;
      auto before = pos;
      while (pos < source.size() && source[pos] >= '0' && source[pos] <= '9')
        ++pos;
      if (pos == before)
        fail();
    }
    if (pos < source.size() && (source[pos] == 'e' || source[pos] == 'E')) {
      ++pos;
      if (pos < source.size() && (source[pos] == '+' || source[pos] == '-'))
        ++pos;
      auto before = pos;
      while (pos < source.size() && source[pos] >= '0' && source[pos] <= '9')
        ++pos;
      if (pos == before)
        fail();
    }
    auto result =
        std::from_chars(source.data() + begin, source.data() + pos, v.numeric);
    if (result.ec != std::errc{} || result.ptr != source.data() + pos ||
        !std::isfinite(v.numeric))
      fail();
    return v;
  }

public:
  explicit JsonReader(std::string_view s) : source(s) {}
  Json parse() {
    auto v = value(0);
    space();
    if (pos != source.size())
      fail();
    return v;
  }
};
} // namespace abc::detail
