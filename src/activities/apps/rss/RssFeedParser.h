#pragma once

#include <expat.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

// Streaming RSS 2.0 / Atom parser: feed raw HTTP chunks into write(); each
// completed <item>/<entry> is reported through the callback with the HTML
// description already flattened to plain text. Bounded buffers throughout —
// nothing scales with feed size, so a multi-hundred-KB feed parses in a few
// KB of RAM.
class RssFeedParser {
 public:
  struct Item {
    const char* title;
    const char* date;
    const std::string& body;  // plain text, ≤ rssstore::kMaxBodyBytes
  };
  // Return false to stop parsing (e.g. article cap reached).
  using ItemFn = std::function<bool(const Item&)>;

  explicit RssFeedParser(ItemFn onItem);
  ~RssFeedParser();

  RssFeedParser(const RssFeedParser&) = delete;
  RssFeedParser& operator=(const RssFeedParser&) = delete;

  // False once parsing is over (limit reached, XML error, or OOM at create).
  bool write(const uint8_t* data, size_t len);

  // True when the callback ended parsing early — the fetch abort that follows
  // is deliberate, not a transport failure.
  bool stoppedByCallback() const { return stoppedByCallback_; }
  int itemCount() const { return itemCount_; }

 private:
  enum class Field : uint8_t { None, Title, Date, Body, RichBody };

  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void XMLCALL endElement(void* userData, const XML_Char* name);
  static void XMLCALL characterData(void* userData, const XML_Char* s, int len);
  void finishItem();

  XML_Parser parser_ = nullptr;
  ItemFn onItem_;
  bool inItem_ = false;
  bool stoppedByCallback_ = false;
  bool failed_ = false;
  Field field_ = Field::None;
  bool bodyIsRich_ = false;  // content:encoded / atom content wins over description
  int itemCount_ = 0;
  char title_[164] = {};
  size_t titleLen_ = 0;
  char date_[52] = {};
  size_t dateLen_ = 0;
  std::string bodyHtml_;   // capped accumulation of the raw description HTML
  std::string bodyPlain_;  // reused per item for the flattened text
};
