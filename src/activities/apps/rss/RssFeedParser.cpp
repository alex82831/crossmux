#include "RssFeedParser.h"

#include <Logging.h>
#include <XmlParserUtils.h>

#include <cstring>

#include "RssStore.h"
#include "util/HtmlToPlainText.h"

namespace {

// Raw description HTML is accumulated to at most this much before flattening;
// long-form full-content feeds are truncated rather than buffered.
constexpr size_t kMaxRawBodyBytes = 4096;

void appendCapped(char* buf, size_t& len, const size_t cap, const char* s, const size_t n) {
  const size_t room = (cap > len + 1) ? cap - len - 1 : 0;
  size_t take = (n < room) ? n : room;
  if (take < n) {  // truncating: never split a UTF-8 sequence
    while (take > 0 && (static_cast<uint8_t>(s[take]) & 0xC0) == 0x80) --take;
  }
  memcpy(buf + len, s, take);
  len += take;
  buf[len] = '\0';
}

// Collapse whitespace runs (incl. newlines) to single spaces, trim both ends.
void collapseWhitespace(char* s) {
  char* out = s;
  bool pendingSpace = false;
  for (const char* p = s; *p != '\0'; ++p) {
    const char c = *p;
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      pendingSpace = out != s;
      continue;
    }
    if (pendingSpace) {
      *out++ = ' ';
      pendingSpace = false;
    }
    *out++ = c;
  }
  *out = '\0';
}

}  // namespace

RssFeedParser::RssFeedParser(ItemFn onItem) : onItem_(std::move(onItem)) {
  parser_ = XML_ParserCreate(nullptr);
  if (parser_ == nullptr) {
    LOG_ERR("RSS", "OOM: XML parser");
    failed_ = true;
    return;
  }
  bodyHtml_.reserve(1024);  // grows toward kMaxRawBodyBytes only on rich feeds
  XML_SetUserData(parser_, this);
  XML_SetElementHandler(parser_, startElement, endElement);
  XML_SetCharacterDataHandler(parser_, characterData);
}

RssFeedParser::~RssFeedParser() { destroyXmlParser(parser_); }

bool RssFeedParser::write(const uint8_t* data, const size_t len) {
  if (failed_ || stoppedByCallback_ || parser_ == nullptr) return false;
  if (XML_Parse(parser_, reinterpret_cast<const char*>(data), static_cast<int>(len), XML_FALSE) != XML_STATUS_OK) {
    if (!stoppedByCallback_) {
      LOG_ERR("RSS", "XML error at %u: %s", static_cast<unsigned>(XML_GetCurrentByteIndex(parser_)),
              XML_ErrorString(XML_GetErrorCode(parser_)));
      failed_ = true;
    }
    return false;
  }
  return true;
}

void XMLCALL RssFeedParser::startElement(void* userData, const XML_Char* name, const XML_Char** /*atts*/) {
  auto* self = static_cast<RssFeedParser*>(userData);
  const char* local = xmlLocalName(name);

  if (!self->inItem_) {
    if (strcmp(local, "item") == 0 || strcmp(local, "entry") == 0) {
      self->inItem_ = true;
      self->titleLen_ = 0;
      self->title_[0] = '\0';
      self->dateLen_ = 0;
      self->date_[0] = '\0';
      self->bodyHtml_.clear();
      self->bodyIsRich_ = false;
    }
    return;
  }

  if (strcmp(local, "title") == 0) {
    self->field_ = Field::Title;
  } else if (strcmp(local, "pubDate") == 0 || strcmp(local, "published") == 0 || strcmp(local, "updated") == 0) {
    if (self->dateLen_ == 0) self->field_ = Field::Date;
  } else if (strcmp(local, "encoded") == 0 || strcmp(local, "content") == 0) {
    // content:encoded / atom:content replace any plain description seen so far.
    if (!self->bodyIsRich_) self->bodyHtml_.clear();
    self->bodyIsRich_ = true;
    self->field_ = Field::RichBody;
  } else if (strcmp(local, "description") == 0 || strcmp(local, "summary") == 0) {
    if (!self->bodyIsRich_) self->field_ = Field::Body;
  }
}

void XMLCALL RssFeedParser::characterData(void* userData, const XML_Char* s, const int len) {
  auto* self = static_cast<RssFeedParser*>(userData);
  const auto n = static_cast<size_t>(len);
  switch (self->field_) {
    case Field::Title:
      appendCapped(self->title_, self->titleLen_, sizeof(self->title_), s, n);
      break;
    case Field::Date:
      appendCapped(self->date_, self->dateLen_, sizeof(self->date_), s, n);
      break;
    case Field::Body:
    case Field::RichBody: {
      const size_t room = (self->bodyHtml_.size() < kMaxRawBodyBytes) ? kMaxRawBodyBytes - self->bodyHtml_.size() : 0;
      self->bodyHtml_.append(s, (n < room) ? n : room);
      break;
    }
    case Field::None:
      break;
  }
}

void XMLCALL RssFeedParser::endElement(void* userData, const XML_Char* name) {
  auto* self = static_cast<RssFeedParser*>(userData);
  self->field_ = Field::None;
  const char* local = xmlLocalName(name);
  if (self->inItem_ && (strcmp(local, "item") == 0 || strcmp(local, "entry") == 0)) {
    self->inItem_ = false;
    self->finishItem();
  }
}

void RssFeedParser::finishItem() {
  // Titles occasionally carry CDATA-wrapped tags or double-encoded entities;
  // flatten them the same way as bodies before the single-line collapse.
  const std::string cleanTitle = htmlToPlainText(title_);
  titleLen_ = 0;
  appendCapped(title_, titleLen_, sizeof(title_), cleanTitle.data(), cleanTitle.size());
  collapseWhitespace(title_);
  collapseWhitespace(date_);
  if (title_[0] == '\0') return;  // headline-less entries are useless rows

  bodyPlain_ = htmlToPlainText(bodyHtml_);
  if (bodyPlain_.size() > rssstore::kMaxBodyBytes) {
    size_t cut = rssstore::kMaxBodyBytes;
    // Never split a UTF-8 sequence: back off over continuation bytes.
    while (cut > 0 && (static_cast<uint8_t>(bodyPlain_[cut]) & 0xC0) == 0x80) --cut;
    bodyPlain_.resize(cut);
  }

  ++itemCount_;
  if (!onItem_(Item{title_, date_, bodyPlain_})) {
    stoppedByCallback_ = true;
    XML_StopParser(parser_, XML_FALSE);
  }
}
