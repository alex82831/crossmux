#pragma once
// 三国霸业 data extracted from the firmware app.
#define SG_CITIES 12
#define SG_GENERALS 20
#define kNeutral 3
#define SG_MAXLEVEL 5
#define SG_MAXTROOPS 60000
#define SG_ACTIONS 3
typedef struct { const char* name; unsigned short adjacency; unsigned char startOwner; unsigned short startTroops; unsigned char farm, market, walls; } CityDef;
typedef struct { const char* name; unsigned char war, wit, owner, city; } GeneralDef;
static const CityDef kCities[SG_CITIES] = {
  {"长安", 1026, kNeutral, 13000, 3, 2, 3},
  {"洛阳", 13, 0, 10000, 3, 3, 3},
  {"邺城", 18, 0, 9000, 3, 3, 2},
  {"许昌", 562, 0, 11000, 3, 3, 2},
  {"徐州", 44, kNeutral, 15000, 2, 2, 2},
  {"寿春", 88, kNeutral, 8000, 2, 2, 2},
  {"建业", 160, 2, 11000, 3, 4, 2},
  {"柴桑", 320, 2, 9000, 3, 3, 2},
  {"江陵", 2688, kNeutral, 9000, 3, 2, 2},
  {"襄阳", 1288, kNeutral, 10000, 2, 2, 3},
  {"汉中", 2561, 1, 9000, 3, 2, 3},
  {"成都", 1280, 1, 11000, 4, 2, 3}
};
static const GeneralDef kGenerals[SG_GENERALS] = {

  {"曹操", 72, 91, 0, 1},
  {"张辽", 92, 78, 0, 3},
  {"夏侯惇", 90, 60, 0, 2},
  {"司马懿", 63, 96, 0, 1},
  {"许褚", 94, 36, 0, 3},
  {"刘备", 73, 78, 1, 11},
  {"关羽", 97, 79, 1, 11},
  {"张飞", 96, 45, 1, 10},
  {"赵云", 96, 76, 1, 11},
  {"诸葛亮", 55, 100, 1, 10},
  {"孙权", 67, 80, 2, 6},
  {"周瑜", 71, 96, 2, 6},
  {"太史慈", 93, 66, 2, 7},
  {"吕蒙", 81, 89, 2, 7},
  {"甘宁", 94, 58, 2, 6},
  {"吕布", 100, 26, kNeutral, 4},
  {"马超", 97, 44, kNeutral, 0},
  {"黄忠", 93, 60, kNeutral, 9},
  {"庞德", 90, 58, kNeutral, 0},
  {"魏延", 89, 69, kNeutral, 8}
};
static const char* kFactionNames[4] = {"魏", "蜀", "吴", "群"};
static const char* kFactionLeaders[3] = {"曹操", "刘备", "孙权"};
static const unsigned short kStartGold[3] = {900, 1000, 1000};
static const unsigned short kStartFood[3] = {1800, 2200, 2000};
static inline unsigned short cityGoldIncome(unsigned char m){return (unsigned short)(m*80+40);}
static inline unsigned short cityFoodIncome(unsigned char f){return (unsigned short)(f*110+60);}
