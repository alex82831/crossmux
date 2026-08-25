#pragma once

#include <cstdint>

#include "SanguoData.h"

// 三国霸业 rules engine: pure game state + turn logic, no rendering. The
// activity layer drives it and draws. The whole state is a flat POD so the
// SD save is a single write.
namespace sanguo {

struct CityState {
  uint8_t owner = kNeutral;
  uint16_t troops = 0;
  uint8_t farm = 1;
  uint8_t market = 1;
  uint8_t walls = 1;
};

struct FactionState {
  uint16_t gold = 0;
  uint16_t food = 0;
};

struct BattleReport {
  bool happened = false;
  bool captured = false;
  uint8_t attackerFaction = 0;
  uint8_t fromCity = 0;
  uint8_t targetCity = 0;
  uint16_t attackerSent = 0;
  uint16_t attackerLost = 0;
  uint16_t defenderHad = 0;
  uint16_t defenderLost = 0;
  int8_t joinedGeneral = -1;  // neutral general recruited on capture
};

// One line per noteworthy AI event, shown in the round report.
struct RoundLog {
  static constexpr int kMaxLines = 6;
  char lines[kMaxLines][72];
  int count = 0;
  void add(const char* fmt, ...);
};

struct GameState {
  uint32_t magic = 0;
  uint16_t round = 1;
  uint8_t playerFaction = 0;
  uint8_t actionsLeft = kActionsPerTurn;
  CityState cities[kCityCount];
  FactionState factions[3];
  uint8_t generalOwner[kGeneralCount] = {};
  uint8_t generalCity[kGeneralCount] = {};
};

enum class ActionResult : uint8_t { Ok, NoActions, NoGold, NoFood, MaxedOut, Invalid };

class Engine {
 public:
  GameState st;

  void newGame(uint8_t playerFaction);

  bool cityIsPlayers(int city) const { return st.cities[city].owner == st.playerFaction; }
  int countCities(uint8_t faction) const;
  int strongestGeneralAt(int city, uint8_t faction, int excludeId = -1) const;
  // Effective combat multiplier (x100) contributed by up to two generals.
  int generalBonusX100(int city, uint8_t faction) const;

  ActionResult develop(int city, int what);  // 0 farm, 1 market, 2 walls
  ActionResult recruit(int city);
  // Attack or reinforce an adjacent city with `troops` from `fromCity`.
  ActionResult march(int fromCity, int toCity, uint16_t troops, BattleReport& report);

  void endPlayerTurn(RoundLog& log, BattleReport* aiBattles, int& aiBattleCount, int maxAiBattles);

  // 0 = ongoing, 1 = player victory, 2 = player defeat
  int outcome() const;

  bool save() const;
  bool load();
  static void wipeSave();
  static bool saveExists();

 private:
  void applyIncome(RoundLog* log);
  void neutralReplenish();
  void aiTurn(uint8_t faction, RoundLog& log, BattleReport* aiBattles, int& aiBattleCount, int maxAiBattles);
  void resolveBattle(int fromCity, int toCity, uint16_t troops, BattleReport& report);
  void moveGeneralsAfterCapture(int city, uint8_t oldOwner, uint8_t newOwner, BattleReport& report);
  uint32_t attackPowerX100(uint16_t troops, int fromCity, uint8_t faction) const;
  uint32_t defensePowerX100(int city) const;
};

}  // namespace sanguo
