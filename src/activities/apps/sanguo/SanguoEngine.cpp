#include "SanguoEngine.h"

#include <HalStorage.h>
#include <Logging.h>
#include <esp_system.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace sanguo {
namespace {
constexpr const char* kSavePath = "/.crosspoint/sanguo.sav";
constexpr uint32_t kSaveMagic = 0x31474753;  // "SGG1"

// ±12% multiplicative battle variance.
uint32_t withVariance(const uint32_t value) {
  const uint32_t swing = value * 12 / 100;
  if (swing == 0) return value;
  return value - swing + (esp_random() % (2 * swing + 1));
}

uint16_t clampTroops(const uint32_t troops) { return troops > kMaxTroops ? kMaxTroops : static_cast<uint16_t>(troops); }
}  // namespace

void RoundLog::add(const char* fmt, ...) {
  if (count >= kMaxLines) return;
  va_list args;
  va_start(args, fmt);
  vsnprintf(lines[count], sizeof(lines[0]), fmt, args);
  va_end(args);
  ++count;
}

void Engine::newGame(const uint8_t playerFaction) {
  st = GameState{};
  st.magic = kSaveMagic;
  st.round = 1;
  st.playerFaction = playerFaction;
  st.actionsLeft = kActionsPerTurn;
  for (int i = 0; i < kCityCount; ++i) {
    st.cities[i].owner = kCities[i].startOwner;
    st.cities[i].troops = kCities[i].startTroops;
    st.cities[i].farm = kCities[i].startFarm;
    st.cities[i].market = kCities[i].startMarket;
    st.cities[i].walls = kCities[i].startWalls;
  }
  for (int f = 0; f < 3; ++f) {
    st.factions[f].gold = kStartGold[f];
    st.factions[f].food = kStartFood[f];
  }
  for (int g = 0; g < kGeneralCount; ++g) {
    st.generalOwner[g] = kGenerals[g].owner;
    st.generalCity[g] = kGenerals[g].city;
  }
}

int Engine::countCities(const uint8_t faction) const {
  int n = 0;
  for (int i = 0; i < kCityCount; ++i) {
    if (st.cities[i].owner == faction) ++n;
  }
  return n;
}

int Engine::strongestGeneralAt(const int city, const uint8_t faction, const int excludeId) const {
  int best = -1;
  int bestWar = -1;
  for (int g = 0; g < kGeneralCount; ++g) {
    if (g == excludeId || st.generalCity[g] != city || st.generalOwner[g] != faction) continue;
    if (kGenerals[g].war > bestWar) {
      bestWar = kGenerals[g].war;
      best = g;
    }
  }
  return best;
}

int Engine::generalBonusX100(const int city, const uint8_t faction) const {
  const int first = strongestGeneralAt(city, faction);
  if (first < 0) return 0;
  int bonus = kGenerals[first].war / 2 + kGenerals[first].wit / 5;
  const int second = strongestGeneralAt(city, faction, first);
  if (second >= 0) bonus += kGenerals[second].war / 5;
  return bonus;
}

ActionResult Engine::develop(const int city, const int what) {
  if (st.actionsLeft == 0) return ActionResult::NoActions;
  CityState& c = st.cities[city];
  if (c.owner != st.playerFaction) return ActionResult::Invalid;
  FactionState& f = st.factions[st.playerFaction];
  uint8_t* level = what == 0 ? &c.farm : what == 1 ? &c.market : &c.walls;
  const uint16_t perLevel = what == 0 ? kFarmCostPerLevel : what == 1 ? kMarketCostPerLevel : kWallCostPerLevel;
  if (*level >= kMaxLevel) return ActionResult::MaxedOut;
  const uint16_t cost = static_cast<uint16_t>(perLevel * *level);
  if (f.gold < cost) return ActionResult::NoGold;
  f.gold -= cost;
  ++*level;
  --st.actionsLeft;
  return ActionResult::Ok;
}

ActionResult Engine::recruit(const int city) {
  if (st.actionsLeft == 0) return ActionResult::NoActions;
  CityState& c = st.cities[city];
  if (c.owner != st.playerFaction) return ActionResult::Invalid;
  FactionState& f = st.factions[st.playerFaction];
  if (f.gold < kRecruitGold) return ActionResult::NoGold;
  if (f.food < kRecruitFood) return ActionResult::NoFood;
  f.gold -= kRecruitGold;
  f.food -= kRecruitFood;
  c.troops = clampTroops(static_cast<uint32_t>(c.troops) + kRecruitTroops);
  --st.actionsLeft;
  return ActionResult::Ok;
}

uint32_t Engine::attackPowerX100(const uint16_t troops, const int fromCity, const uint8_t faction) const {
  return static_cast<uint32_t>(troops) * (100 + generalBonusX100(fromCity, faction));
}

uint32_t Engine::defensePowerX100(const int city) const {
  const CityState& c = st.cities[city];
  const uint32_t base = static_cast<uint32_t>(c.troops) * (100 + c.walls * 8 + generalBonusX100(city, c.owner));
  return base * 115 / 100;  // defender's ground advantage
}

void Engine::moveGeneralsAfterCapture(const int city, const uint8_t oldOwner, const uint8_t newOwner,
                                      BattleReport& report) {
  for (int g = 0; g < kGeneralCount; ++g) {
    if (st.generalCity[g] != city || st.generalOwner[g] != oldOwner) continue;
    if (oldOwner == kNeutral) {
      // Unaffiliated generals throw in with the conqueror.
      st.generalOwner[g] = newOwner;
      report.joinedGeneral = static_cast<int8_t>(g);
      continue;
    }
    // Faction generals flee to another owned city, or join the victor when
    // their realm has none left.
    int refuge = -1;
    for (int i = 0; i < kCityCount; ++i) {
      if (i != city && st.cities[i].owner == oldOwner) {
        refuge = i;
        break;
      }
    }
    if (refuge >= 0) {
      st.generalCity[g] = static_cast<uint8_t>(refuge);
    } else {
      st.generalOwner[g] = newOwner;
      report.joinedGeneral = static_cast<int8_t>(g);
    }
  }
}

void Engine::resolveBattle(const int fromCity, const int toCity, const uint16_t troops, BattleReport& report) {
  CityState& from = st.cities[fromCity];
  CityState& target = st.cities[toCity];
  const uint8_t attacker = from.owner;
  const uint8_t defender = target.owner;

  report.happened = true;
  report.attackerFaction = attacker;
  report.fromCity = static_cast<uint8_t>(fromCity);
  report.targetCity = static_cast<uint8_t>(toCity);
  report.attackerSent = troops;
  report.defenderHad = target.troops;

  from.troops = static_cast<uint16_t>(from.troops - troops);

  const uint32_t atk = withVariance(attackPowerX100(troops, fromCity, attacker));
  const uint32_t def = withVariance(defensePowerX100(toCity));

  if (target.troops == 0 || atk > def) {
    // City falls. Attacker casualties scale with how close the fight was.
    const uint32_t lost = def == 0 ? 0 : static_cast<uint32_t>(troops) * def / atk * 55 / 100;
    report.attackerLost = clampTroops(lost > troops ? troops : lost);
    report.defenderLost = target.troops;
    report.captured = true;
    moveGeneralsAfterCapture(toCity, defender, attacker, report);
    target.owner = attacker;
    target.troops = static_cast<uint16_t>(troops - report.attackerLost);
    // The marching generals occupy the new city.
    const int lead = strongestGeneralAt(fromCity, attacker);
    if (lead >= 0) st.generalCity[lead] = static_cast<uint8_t>(toCity);
  } else {
    // Repelled: heavy attacker losses, meaningful defender attrition.
    uint32_t atkLost = static_cast<uint32_t>(troops) * def / (atk == 0 ? 1 : atk) * 50 / 100;
    if (atkLost > troops) atkLost = troops;
    uint32_t defLost = static_cast<uint32_t>(target.troops) * atk / def * 35 / 100;
    if (defLost > target.troops) defLost = target.troops;
    report.attackerLost = static_cast<uint16_t>(atkLost);
    report.defenderLost = static_cast<uint16_t>(defLost);
    report.captured = false;
    target.troops = static_cast<uint16_t>(target.troops - defLost);
    from.troops = clampTroops(static_cast<uint32_t>(from.troops) + (troops - atkLost));  // survivors return
  }
}

ActionResult Engine::march(const int fromCity, const int toCity, const uint16_t troops, BattleReport& report) {
  if (st.actionsLeft == 0) return ActionResult::NoActions;
  CityState& from = st.cities[fromCity];
  if (from.owner != st.playerFaction) return ActionResult::Invalid;
  if ((kCities[fromCity].adjacency & (1U << toCity)) == 0) return ActionResult::Invalid;
  if (troops == 0 || troops > from.troops) return ActionResult::Invalid;

  CityState& target = st.cities[toCity];
  if (target.owner == st.playerFaction) {
    from.troops = static_cast<uint16_t>(from.troops - troops);
    target.troops = clampTroops(static_cast<uint32_t>(target.troops) + troops);
    report.happened = false;
  } else {
    resolveBattle(fromCity, toCity, troops, report);
  }
  --st.actionsLeft;
  return ActionResult::Ok;
}

void Engine::applyIncome(RoundLog* log) {
  for (int f = 0; f < 3; ++f) {
    uint32_t gold = st.factions[f].gold;
    uint32_t food = st.factions[f].food;
    uint32_t upkeepTroops = 0;
    for (int i = 0; i < kCityCount; ++i) {
      if (st.cities[i].owner != f) continue;
      gold += cityGoldIncome(st.cities[i].market);
      food += cityFoodIncome(st.cities[i].farm);
      upkeepTroops += st.cities[i].troops;
    }
    const uint32_t upkeep = upkeepTroops / 10;
    if (food >= upkeep) {
      food -= upkeep;
    } else {
      // Starvation: the shortfall deserts, spread across the largest garrisons.
      uint32_t shortfall = (upkeep - food) * 10;
      food = 0;
      if (log != nullptr && f == st.playerFaction && shortfall > 0) {
        log->add("粮草不足，部队出现逃亡");
      }
      for (int i = 0; i < kCityCount && shortfall > 0; ++i) {
        if (st.cities[i].owner != f) continue;
        const uint32_t desert = st.cities[i].troops / 5;
        const uint32_t take = desert < shortfall ? desert : shortfall;
        st.cities[i].troops = static_cast<uint16_t>(st.cities[i].troops - take);
        shortfall -= take;
      }
    }
    st.factions[f].gold = gold > 0xFFFF ? 0xFFFF : static_cast<uint16_t>(gold);
    st.factions[f].food = food > 0xFFFF ? 0xFFFF : static_cast<uint16_t>(food);
  }
}

void Engine::neutralReplenish() {
  for (int i = 0; i < kCityCount; ++i) {
    if (st.cities[i].owner != kNeutral) continue;
    if (st.cities[i].troops < kCities[i].startTroops) {
      st.cities[i].troops = clampTroops(static_cast<uint32_t>(st.cities[i].troops) + 300);
    }
  }
}

void Engine::aiTurn(const uint8_t faction, RoundLog& log, BattleReport* aiBattles, int& aiBattleCount,
                    const int maxAiBattles) {
  if (countCities(faction) == 0) return;
  FactionState& f = st.factions[faction];
  bool attackedThisTurn = false;

  for (int action = 0; action < kActionsPerTurn; ++action) {
    // 1) Keep the army fed: two turns of upkeep in reserve.
    uint32_t troopsTotal = 0;
    for (int i = 0; i < kCityCount; ++i) {
      if (st.cities[i].owner == faction) troopsTotal += st.cities[i].troops;
    }
    if (f.food < troopsTotal / 10 * 2) {
      int lowFarm = -1;
      for (int i = 0; i < kCityCount; ++i) {
        if (st.cities[i].owner != faction || st.cities[i].farm >= kMaxLevel) continue;
        if (lowFarm < 0 || st.cities[i].farm < st.cities[lowFarm].farm) lowFarm = i;
      }
      if (lowFarm >= 0 && f.gold >= kFarmCostPerLevel * st.cities[lowFarm].farm) {
        f.gold -= kFarmCostPerLevel * st.cities[lowFarm].farm;
        ++st.cities[lowFarm].farm;
        continue;
      }
    }

    // 2) One good attack per turn when a clear local advantage exists.
    if (!attackedThisTurn) {
      int bestFrom = -1, bestTarget = -1;
      uint32_t bestRatio = 0;
      for (int i = 0; i < kCityCount; ++i) {
        if (st.cities[i].owner != faction || st.cities[i].troops < 6000) continue;
        const uint16_t sendable = static_cast<uint16_t>(st.cities[i].troops * 8 / 10);
        for (int t = 0; t < kCityCount; ++t) {
          if ((kCities[i].adjacency & (1U << t)) == 0 || st.cities[t].owner == faction) continue;
          const uint32_t atk = attackPowerX100(sendable, i, faction);
          const uint32_t def = defensePowerX100(t) + 1;
          const uint32_t ratio = atk * 100 / def;
          if (ratio > bestRatio) {
            bestRatio = ratio;
            bestFrom = i;
            bestTarget = t;
          }
        }
      }
      if (bestFrom >= 0 && bestRatio >= 140) {
        const uint16_t sendable = static_cast<uint16_t>(st.cities[bestFrom].troops * 8 / 10);
        BattleReport report;
        resolveBattle(bestFrom, bestTarget, sendable, report);
        attackedThisTurn = true;
        if (aiBattleCount < maxAiBattles) aiBattles[aiBattleCount++] = report;
        log.add("%s%s%s（%s）", kFactionNames[faction], report.captured ? "攻占" : "进攻", kCities[bestTarget].name,
                report.captured ? "得手" : "被击退");
        continue;
      }
    }

    // 3) Recruit at the weakest frontier city.
    if (f.gold >= kRecruitGold && f.food >= kRecruitFood) {
      int weakest = -1;
      for (int i = 0; i < kCityCount; ++i) {
        if (st.cities[i].owner != faction) continue;
        bool border = false;
        for (int t = 0; t < kCityCount; ++t) {
          if ((kCities[i].adjacency & (1U << t)) != 0 && st.cities[t].owner != faction) border = true;
        }
        if (!border || st.cities[i].troops >= 20000) continue;
        if (weakest < 0 || st.cities[i].troops < st.cities[weakest].troops) weakest = i;
      }
      if (weakest >= 0) {
        f.gold -= kRecruitGold;
        f.food -= kRecruitFood;
        st.cities[weakest].troops = clampTroops(static_cast<uint32_t>(st.cities[weakest].troops) + kRecruitTroops);
        continue;
      }
    }

    // 4) Otherwise grow the economy.
    int lowMarket = -1;
    for (int i = 0; i < kCityCount; ++i) {
      if (st.cities[i].owner != faction || st.cities[i].market >= kMaxLevel) continue;
      if (lowMarket < 0 || st.cities[i].market < st.cities[lowMarket].market) lowMarket = i;
    }
    if (lowMarket >= 0 && f.gold >= kMarketCostPerLevel * st.cities[lowMarket].market) {
      f.gold -= kMarketCostPerLevel * st.cities[lowMarket].market;
      ++st.cities[lowMarket].market;
    }
  }
}

void Engine::endPlayerTurn(RoundLog& log, BattleReport* aiBattles, int& aiBattleCount, const int maxAiBattles) {
  aiBattleCount = 0;
  for (uint8_t f = 0; f < 3; ++f) {
    if (f == st.playerFaction) continue;
    aiTurn(f, log, aiBattles, aiBattleCount, maxAiBattles);
  }
  applyIncome(&log);
  neutralReplenish();
  ++st.round;
  st.actionsLeft = kActionsPerTurn;
  save();
}

int Engine::outcome() const {
  const int player = countCities(st.playerFaction);
  if (player == kCityCount) return 1;
  if (player == 0) return 2;
  return 0;
}

bool Engine::save() const {
  HalFile file;
  if (!Storage.openFileForWrite("SGG", kSavePath, file)) {
    LOG_ERR("SGG", "save open failed");
    return false;
  }
  GameState copy = st;
  copy.magic = kSaveMagic;
  return file.write(&copy, sizeof(copy)) == sizeof(copy);
}

bool Engine::load() {
  HalFile file;
  if (!Storage.openFileForRead("SGG", kSavePath, file)) return false;
  GameState loaded;
  if (file.read(&loaded, sizeof(loaded)) != static_cast<int>(sizeof(loaded))) return false;
  if (loaded.magic != kSaveMagic || loaded.playerFaction > 2) return false;
  st = loaded;
  return true;
}

void Engine::wipeSave() { Storage.remove(kSavePath); }

bool Engine::saveExists() { return Storage.exists(kSavePath); }

}  // namespace sanguo
