#pragma once

#include <cstdint>

// 三国霸业 static data: the 12-city map, the four factions, and the general
// roster. All flash-resident; glyph coverage comes from the cn_apps_chars.txt
// scan of this directory (see chinese-build.md).
namespace sanguo {

constexpr int kCityCount = 12;
constexpr int kFactionCount = 4;  // 魏 蜀 吴 + 群(neutral)
constexpr int kGeneralCount = 20;
constexpr uint8_t kNeutral = 3;
constexpr uint8_t kNoCity = 0xFF;

constexpr int kActionsPerTurn = 3;
constexpr int kMaxLevel = 5;
constexpr uint16_t kMaxTroops = 60000;
constexpr uint16_t kRecruitTroops = 2000;
constexpr uint16_t kRecruitGold = 300;
constexpr uint16_t kRecruitFood = 500;
constexpr uint16_t kFarmCostPerLevel = 150;
constexpr uint16_t kMarketCostPerLevel = 150;
constexpr uint16_t kWallCostPerLevel = 200;

struct CityDef {
  const char* name;
  uint16_t adjacency;  // bitmask of connected city indices
  uint8_t startOwner;
  uint16_t startTroops;
  uint8_t startFarm;
  uint8_t startMarket;
  uint8_t startWalls;
};

constexpr uint16_t adj(const int a, const int b) { return static_cast<uint16_t>((1U << a) | (1U << b)); }

// Index reference: 0长安 1洛阳 2邺城 3许昌 4徐州 5寿春 6建业 7柴桑 8江陵 9襄阳 10汉中 11成都
constexpr CityDef kCities[kCityCount] = {
    //            adjacency                                owner troops farm market walls
    {"长安", static_cast<uint16_t>((1U << 1) | (1U << 10)), kNeutral, 13000, 3, 2, 3},
    {"洛阳", static_cast<uint16_t>((1U << 0) | (1U << 2) | (1U << 3)), 0, 10000, 3, 3, 3},
    {"邺城", static_cast<uint16_t>((1U << 1) | (1U << 4)), 0, 9000, 3, 3, 2},
    {"许昌", static_cast<uint16_t>((1U << 1) | (1U << 4) | (1U << 5) | (1U << 9)), 0, 11000, 3, 3, 2},
    {"徐州", static_cast<uint16_t>((1U << 2) | (1U << 3) | (1U << 5)), kNeutral, 15000, 2, 2, 2},
    {"寿春", static_cast<uint16_t>((1U << 3) | (1U << 4) | (1U << 6)), kNeutral, 8000, 2, 2, 2},
    {"建业", static_cast<uint16_t>((1U << 5) | (1U << 7)), 2, 11000, 3, 4, 2},
    {"柴桑", static_cast<uint16_t>((1U << 6) | (1U << 8)), 2, 9000, 3, 3, 2},
    {"江陵", static_cast<uint16_t>((1U << 7) | (1U << 9) | (1U << 11)), kNeutral, 9000, 3, 2, 2},
    {"襄阳", static_cast<uint16_t>((1U << 3) | (1U << 8) | (1U << 10)), kNeutral, 10000, 2, 2, 3},
    {"汉中", static_cast<uint16_t>((1U << 0) | (1U << 9) | (1U << 11)), 1, 9000, 3, 2, 3},
    {"成都", static_cast<uint16_t>((1U << 10) | (1U << 8)), 1, 11000, 4, 2, 3},
};

struct GeneralDef {
  const char* name;
  uint8_t war;    // 武力
  uint8_t wit;    // 智力
  uint8_t owner;  // starting faction (kNeutral for unaffiliated)
  uint8_t city;   // starting city index
};

constexpr GeneralDef kGenerals[kGeneralCount] = {
    {"曹操", 72, 91, 0, 1},         {"张辽", 92, 78, 0, 3},        {"夏侯惇", 90, 60, 0, 2},
    {"司马懿", 63, 96, 0, 1},       {"许褚", 94, 36, 0, 3},        {"刘备", 73, 78, 1, 11},
    {"关羽", 97, 79, 1, 11},        {"张飞", 96, 45, 1, 10},       {"赵云", 96, 76, 1, 11},
    {"诸葛亮", 55, 100, 1, 10},     {"孙权", 67, 80, 2, 6},        {"周瑜", 71, 96, 2, 6},
    {"太史慈", 93, 66, 2, 7},       {"吕蒙", 81, 89, 2, 7},        {"甘宁", 94, 58, 2, 6},
    {"吕布", 100, 26, kNeutral, 4}, {"马超", 97, 44, kNeutral, 0}, {"黄忠", 93, 60, kNeutral, 9},
    {"庞德", 90, 58, kNeutral, 0},  {"魏延", 89, 69, kNeutral, 8},
};

constexpr const char* kFactionNames[kFactionCount] = {"魏", "蜀", "吴", "群"};
constexpr const char* kFactionLeaders[3] = {"曹操", "刘备", "孙权"};

// Per-faction starting treasury: 魏 fields more cities, 蜀/吴 start richer per
// city so the three openings play at comparable tempo.
constexpr uint16_t kStartGold[3] = {900, 1000, 1000};
constexpr uint16_t kStartFood[3] = {1800, 2200, 2000};

inline uint16_t cityGoldIncome(const uint8_t market) { return static_cast<uint16_t>(market * 80 + 40); }
inline uint16_t cityFoodIncome(const uint8_t farm) { return static_cast<uint16_t>(farm * 110 + 60); }

}  // namespace sanguo
