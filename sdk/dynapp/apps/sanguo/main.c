// 三国霸业 — turn-based Three Kingdoms strategy. 12-city map, 20 historical
// generals, gold/food economy with upkeep, develop/recruit/march, combat with
// wall + general bonuses and ±12% variance, two heuristic AI factions,
// recruit neutral generals on capture, autosave. Engine ported faithfully
// from the firmware app. Ported to .eapp.

#include "app.h"
#include "data.h"

// ---- state --------------------------------------------------------------
typedef struct {
  unsigned char owner;
  unsigned short troops;
  unsigned char farm, market, walls;
} City;

static struct {
  unsigned int magic;
  unsigned short round;
  unsigned char playerFaction;
  unsigned char actionsLeft;
  City cities[SG_CITIES];
  unsigned short gold[3], food[3];
  unsigned char genOwner[SG_GENERALS], genCity[SG_GENERALS];
} st;

#define SG_MAGIC 0x33474E53u  // "SNG3"
#define FARM_COST 150
#define MARKET_COST 150
#define WALL_COST 200
#define RECRUIT_GOLD 300
#define RECRUIT_FOOD 500
#define RECRUIT_TROOPS 2000

// ---- view -----------------------------------------------------------------
enum { V_MENU, V_FACTION, V_PLAY, V_HELP, V_ABOUT_MENU };
static int g_view;
static int g_menuSel;
static int g_selCity;    // cursor on the map (play view)
static int g_actionSel;  // action submenu index, -1 = map cursor mode
static char g_log[6][48];
static int g_logN;
static int g_outcome;  // 0 ongoing, 1 win, 2 loss
static AppAbout g_about;

static const char* kHelp =
    "目标：夺取全部十二城，一统天下。每回合三次行动：开垦增粮、通商增金、筑城增防、"
    "征兵（金300粮500得2000兵）、出征相邻城。战力由兵力、城防与坐镇武将决定，守方有地"
    "利加成，±12%战场变数。攻下中立城可招揽其武将。回合末按城池产出、按兵力耗粮，缺粮"
    "士卒逃亡。魏地广、蜀吴府库殷实，宜尽早抢占中立城。";

// ---- helpers --------------------------------------------------------------
static uint16_t clampT(uint32_t t) { return t > SG_MAXTROOPS ? SG_MAXTROOPS : (uint16_t)t; }

static int countCities(int f) {
  int n = 0;
  for (int i = 0; i < SG_CITIES; ++i)
    if (st.cities[i].owner == f) ++n;
  return n;
}

static int strongestGeneralAt(int city, int faction, int exclude) {
  int best = -1, bestScore = -1;
  for (int g = 0; g < SG_GENERALS; ++g) {
    if (g == exclude || st.genCity[g] != city || st.genOwner[g] != faction) continue;
    const int score = kGenerals[g].war + kGenerals[g].wit;
    if (score > bestScore) {
      bestScore = score;
      best = g;
    }
  }
  return best;
}

static int generalBonusX100(int city, int faction) {
  const int first = strongestGeneralAt(city, faction, -1);
  if (first < 0) return 0;
  int bonus = kGenerals[first].war / 2 + kGenerals[first].wit / 5;
  const int second = strongestGeneralAt(city, faction, first);
  if (second >= 0) bonus += kGenerals[second].war / 5;
  return bonus;
}

static uint32_t attackPower(uint16_t troops, int fromCity, int faction) {
  return (uint32_t)troops * (100 + generalBonusX100(fromCity, faction));
}
static uint32_t defensePower(int city) {
  const City* c = &st.cities[city];
  const uint32_t base = (uint32_t)c->troops * (100 + c->walls * 8 + generalBonusX100(city, c->owner));
  return base * 115 / 100;
}
static uint32_t withVariance(const CpApi* api, uint32_t v) {
  const uint32_t swing = v * 12 / 100;
  if (swing == 0) return v;
  return v - swing + (api->random_u32() % (2 * swing + 1));
}

static void logAdd(const char* s) {
  if (g_logN >= 6) {  // scroll
    for (int i = 1; i < 6; ++i) memcpy(g_log[i - 1], g_log[i], sizeof(g_log[0]));
    g_logN = 5;
  }
  cp_snprintf(g_log[g_logN], sizeof(g_log[0]), "%s", s);
  ++g_logN;
}

static void newGame(int faction) {
  memset(&st, 0, sizeof(st));
  st.magic = SG_MAGIC;
  st.round = 1;
  st.playerFaction = (unsigned char)faction;
  st.actionsLeft = SG_ACTIONS;
  for (int i = 0; i < SG_CITIES; ++i) {
    st.cities[i].owner = kCities[i].startOwner;
    st.cities[i].troops = kCities[i].startTroops;
    st.cities[i].farm = kCities[i].farm;
    st.cities[i].market = kCities[i].market;
    st.cities[i].walls = kCities[i].walls;
  }
  for (int f = 0; f < 3; ++f) {
    st.gold[f] = kStartGold[f];
    st.food[f] = kStartFood[f];
  }
  for (int g = 0; g < SG_GENERALS; ++g) {
    st.genOwner[g] = kGenerals[g].owner;
    st.genCity[g] = kGenerals[g].city;
  }
  g_logN = 0;
  g_outcome = 0;
  g_selCity = 0;
  for (int i = 0; i < SG_CITIES; ++i)
    if (st.cities[i].owner == faction) {
      g_selCity = i;
      break;
    }
}

static void save(const CpApi* api) { api->file_write("save.bin", &st, sizeof(st)); }
static int loadSave(const CpApi* api) {
  int n = api->file_read("save.bin", &st, sizeof(st));
  return n == (int)sizeof(st) && st.magic == SG_MAGIC;
}

// ---- combat ---------------------------------------------------------------
static void moveGeneralsAfterCapture(int city, int oldOwner, int newOwner, char* joinMsg, int joinCap) {
  for (int g = 0; g < SG_GENERALS; ++g) {
    if (st.genCity[g] != city || st.genOwner[g] != oldOwner) continue;
    if (oldOwner == kNeutral) {
      st.genOwner[g] = (unsigned char)newOwner;
      if (joinMsg && joinMsg[0] == 0) cp_snprintf(joinMsg, joinCap, "%s归顺", kGenerals[g].name);
      continue;
    }
    int refuge = -1;
    for (int i = 0; i < SG_CITIES; ++i)
      if (i != city && st.cities[i].owner == oldOwner) {
        refuge = i;
        break;
      }
    if (refuge >= 0)
      st.genCity[g] = (unsigned char)refuge;
    else {
      st.genOwner[g] = (unsigned char)newOwner;
      if (joinMsg && joinMsg[0] == 0) cp_snprintf(joinMsg, joinCap, "%s归顺", kGenerals[g].name);
    }
  }
}

// Returns 1 if the city was captured.
static int resolveBattle(const CpApi* api, int fromCity, int toCity, uint16_t troops, char* outMsg, int outCap) {
  City* from = &st.cities[fromCity];
  City* target = &st.cities[toCity];
  const int attacker = from->owner, defender = target->owner;
  from->troops = (uint16_t)(from->troops - troops);
  const uint32_t atk = withVariance(api, attackPower(troops, fromCity, attacker));
  const uint32_t def = withVariance(api, defensePower(toCity));

  if (target->troops == 0 || atk > def) {
    uint32_t lost = (def == 0) ? 0 : (uint32_t)troops * def / (atk == 0 ? 1 : atk) * 55 / 100;
    if (lost > troops) lost = troops;
    char joinMsg[24];
    joinMsg[0] = 0;
    moveGeneralsAfterCapture(toCity, defender, attacker, joinMsg, sizeof(joinMsg));
    target->owner = (unsigned char)attacker;
    target->troops = (uint16_t)(troops - lost);
    const int lead = strongestGeneralAt(fromCity, attacker, -1);
    if (lead >= 0) st.genCity[lead] = (unsigned char)toCity;
    if (outMsg)
      cp_snprintf(outMsg, outCap, "%s攻占%s%s%s", kFactionNames[attacker], kCities[toCity].name, joinMsg[0] ? "，" : "",
                  joinMsg);
    return 1;
  }
  uint32_t atkLost = (uint32_t)troops * def / (atk == 0 ? 1 : atk) * 50 / 100;
  if (atkLost > troops) atkLost = troops;
  uint32_t defLost = (uint32_t)target->troops * atk / (def == 0 ? 1 : def) * 35 / 100;
  if (defLost > target->troops) defLost = target->troops;
  target->troops = (uint16_t)(target->troops - defLost);
  from->troops = clampT((uint32_t)from->troops + (troops - atkLost));
  if (outMsg) cp_snprintf(outMsg, outCap, "%s进攻%s被击退", kFactionNames[attacker], kCities[toCity].name);
  return 0;
}

// ---- economy / turn -------------------------------------------------------
static void applyIncome(void) {
  for (int f = 0; f < 3; ++f) {
    uint32_t gold = st.gold[f], food = st.food[f], upkeepTroops = 0;
    for (int i = 0; i < SG_CITIES; ++i) {
      if (st.cities[i].owner != f) continue;
      gold += cityGoldIncome(st.cities[i].market);
      food += cityFoodIncome(st.cities[i].farm);
      upkeepTroops += st.cities[i].troops;
    }
    const uint32_t upkeep = upkeepTroops / 10;
    if (food >= upkeep)
      food -= upkeep;
    else {
      uint32_t shortfall = (upkeep - food) * 10;
      food = 0;
      if (f == st.playerFaction) logAdd("粮草不足，部队出现逃亡");
      for (int i = 0; i < SG_CITIES && shortfall > 0; ++i) {
        if (st.cities[i].owner != f) continue;
        const uint32_t desert = st.cities[i].troops / 5;
        const uint32_t take = desert < shortfall ? desert : shortfall;
        st.cities[i].troops = (uint16_t)(st.cities[i].troops - take);
        shortfall -= take;
      }
    }
    st.gold[f] = gold > 0xFFFF ? 0xFFFF : (uint16_t)gold;
    st.food[f] = food > 0xFFFF ? 0xFFFF : (uint16_t)food;
  }
}

static void aiTurn(const CpApi* api, int faction) {
  if (countCities(faction) == 0) return;
  int attacked = 0;
  for (int action = 0; action < SG_ACTIONS; ++action) {
    uint32_t troopsTotal = 0;
    for (int i = 0; i < SG_CITIES; ++i)
      if (st.cities[i].owner == faction) troopsTotal += st.cities[i].troops;
    if (st.food[faction] < troopsTotal / 10 * 2) {
      int lowFarm = -1;
      for (int i = 0; i < SG_CITIES; ++i) {
        if (st.cities[i].owner != faction || st.cities[i].farm >= SG_MAXLEVEL) continue;
        if (lowFarm < 0 || st.cities[i].farm < st.cities[lowFarm].farm) lowFarm = i;
      }
      if (lowFarm >= 0 && st.gold[faction] >= FARM_COST * st.cities[lowFarm].farm) {
        st.gold[faction] -= FARM_COST * st.cities[lowFarm].farm;
        ++st.cities[lowFarm].farm;
        continue;
      }
    }
    if (!attacked) {
      int bestFrom = -1, bestTarget = -1;
      uint32_t bestRatio = 0;
      for (int i = 0; i < SG_CITIES; ++i) {
        if (st.cities[i].owner != faction || st.cities[i].troops < 6000) continue;
        const uint16_t sendable = (uint16_t)(st.cities[i].troops * 8 / 10);
        for (int t = 0; t < SG_CITIES; ++t) {
          if ((kCities[i].adjacency & (1U << t)) == 0 || st.cities[t].owner == faction) continue;
          const uint32_t ratio = attackPower(sendable, i, faction) * 100 / (defensePower(t) + 1);
          if (ratio > bestRatio) {
            bestRatio = ratio;
            bestFrom = i;
            bestTarget = t;
          }
        }
      }
      if (bestFrom >= 0 && bestRatio >= 140) {
        const uint16_t sendable = (uint16_t)(st.cities[bestFrom].troops * 8 / 10);
        char msg[48];
        resolveBattle(api, bestFrom, bestTarget, sendable, msg, sizeof(msg));
        logAdd(msg);
        attacked = 1;
        continue;
      }
    }
    if (st.gold[faction] >= RECRUIT_GOLD && st.food[faction] >= RECRUIT_FOOD) {
      int weakest = -1;
      for (int i = 0; i < SG_CITIES; ++i) {
        if (st.cities[i].owner != faction) continue;
        int border = 0;
        for (int t = 0; t < SG_CITIES; ++t)
          if ((kCities[i].adjacency & (1U << t)) && st.cities[t].owner != faction) border = 1;
        if (!border || st.cities[i].troops >= 20000) continue;
        if (weakest < 0 || st.cities[i].troops < st.cities[weakest].troops) weakest = i;
      }
      if (weakest >= 0) {
        st.gold[faction] -= RECRUIT_GOLD;
        st.food[faction] -= RECRUIT_FOOD;
        st.cities[weakest].troops = clampT((uint32_t)st.cities[weakest].troops + RECRUIT_TROOPS);
        continue;
      }
    }
    int lowMarket = -1;
    for (int i = 0; i < SG_CITIES; ++i) {
      if (st.cities[i].owner != faction || st.cities[i].market >= SG_MAXLEVEL) continue;
      if (lowMarket < 0 || st.cities[i].market < st.cities[lowMarket].market) lowMarket = i;
    }
    if (lowMarket >= 0 && st.gold[faction] >= MARKET_COST * st.cities[lowMarket].market) {
      st.gold[faction] -= MARKET_COST * st.cities[lowMarket].market;
      ++st.cities[lowMarket].market;
    }
  }
}

static void checkOutcome(void) {
  if (countCities(st.playerFaction) == SG_CITIES)
    g_outcome = 1;
  else if (countCities(st.playerFaction) == 0)
    g_outcome = 2;
}

static void endTurn(const CpApi* api) {
  for (int f = 0; f < 3; ++f)
    if (f != st.playerFaction) aiTurn(api, f);
  applyIncome();
  ++st.round;
  st.actionsLeft = SG_ACTIONS;
  checkOutcome();
  save(api);
}

// ---- player actions -------------------------------------------------------
static int adjacentEnemy(int city, int* out, int cap) {
  int n = 0;
  for (int t = 0; t < SG_CITIES && n < cap; ++t)
    if ((kCities[city].adjacency & (1U << t)) && st.cities[t].owner != st.playerFaction) out[n++] = t;
  return n;
}

static const char* kActions[5] = {"开垦", "通商", "筑城", "征兵", "出征"};
static int g_marchTarget = -1;  // when choosing a march destination

static void doAction(const CpApi* api, int a) {
  if (st.actionsLeft == 0) return;
  City* c = &st.cities[g_selCity];
  if (c->owner != st.playerFaction) return;
  const int pf = st.playerFaction;
  if (a == 0 && c->farm < SG_MAXLEVEL && st.gold[pf] >= FARM_COST * c->farm) {
    st.gold[pf] -= FARM_COST * c->farm;
    ++c->farm;
    --st.actionsLeft;
  } else if (a == 1 && c->market < SG_MAXLEVEL && st.gold[pf] >= MARKET_COST * c->market) {
    st.gold[pf] -= MARKET_COST * c->market;
    ++c->market;
    --st.actionsLeft;
  } else if (a == 2 && c->walls < SG_MAXLEVEL && st.gold[pf] >= WALL_COST * c->walls) {
    st.gold[pf] -= WALL_COST * c->walls;
    ++c->walls;
    --st.actionsLeft;
  } else if (a == 3 && st.gold[pf] >= RECRUIT_GOLD && st.food[pf] >= RECRUIT_FOOD) {
    st.gold[pf] -= RECRUIT_GOLD;
    st.food[pf] -= RECRUIT_FOOD;
    c->troops = clampT((uint32_t)c->troops + RECRUIT_TROOPS);
    --st.actionsLeft;
  } else if (a == 4) {
    int enemies[4];
    if (adjacentEnemy(g_selCity, enemies, 4) > 0 && c->troops >= 1000) {
      g_marchTarget = enemies[0];  // simplest: attack first adjacent; cycle via Right
    }
    return;  // handled in the march sub-flow
  }
  checkOutcome();
  save(api);
}

// ---- app lifecycle --------------------------------------------------------
static int g_hasSave;

static int32_t on_enter(const CpApi* api) {
  g_view = V_MENU;
  g_menuSel = 0;
  g_actionSel = -1;
  g_marchTarget = -1;
  g_hasSave = loadSave(api);  // a valid save enables "继续天下"
  return 0;
}

static void startMenu(const CpApi* api) {
  (void)api;
  g_view = V_MENU;
  g_menuSel = 0;
}

static uint32_t on_loop(const CpApi* api, const CpInput* in) {
  int repaint = 0;
  if (app_about_input(api, in, &g_about, 1, &repaint)) return repaint ? CP_LOOP_RENDER : CP_LOOP_IDLE;

  if (g_view == V_MENU) {
    if (in->released & CP_BTN_BACK) return CP_LOOP_EXIT;
    const int count = g_hasSave ? 4 : 3;  // [继续] 新游戏 / 玩法 / 关于
    if (app_menu_input(api, in, &g_menuSel, count)) {
      int item = g_menuSel;
      if (!g_hasSave) ++item;  // skip the hidden "continue" slot
      if (item == 0) {
        g_view = V_PLAY;
        g_actionSel = -1;
        g_outcome = 0;
      } else if (item == 1) {
        g_view = V_FACTION;
        g_menuSel = 0;
      } else if (item == 2)
        g_view = V_HELP;
      else
        g_view = V_ABOUT_MENU;
      return CP_LOOP_RENDER;
    }
    return CP_LOOP_IDLE;
  }
  if (g_view == V_HELP || g_view == V_ABOUT_MENU) {
    if ((in->released & CP_BTN_BACK) || (in->released & CP_BTN_CONFIRM)) {
      startMenu(api);
      return CP_LOOP_RENDER;
    }
    return CP_LOOP_IDLE;
  }
  if (g_view == V_FACTION) {
    if (in->released & CP_BTN_BACK) {
      startMenu(api);
      return CP_LOOP_RENDER;
    }
    if (app_menu_input(api, in, &g_menuSel, 3)) {
      newGame(g_menuSel);
      g_view = V_PLAY;
      g_actionSel = -1;
      return CP_LOOP_RENDER;
    }
    return CP_LOOP_IDLE;
  }

  // ---- play ----
  if (g_outcome) {
    if (in->released & CP_BTN_CONFIRM) {
      api->file_delete("save.bin");
      startMenu(api);
      return CP_LOOP_RENDER;
    }
    return CP_LOOP_IDLE;
  }
  if (in->released & CP_BTN_BACK) {
    if (g_marchTarget >= 0) {
      g_marchTarget = -1;
      return CP_LOOP_RENDER;
    }
    if (g_actionSel >= 0) {
      g_actionSel = -1;
      return CP_LOOP_RENDER;
    }
    save(api);
    startMenu(api);
    return CP_LOOP_RENDER;
  }

  // March target selection sub-mode.
  if (g_marchTarget >= 0) {
    int enemies[4];
    const int ne = adjacentEnemy(g_selCity, enemies, 4);
    int idx = 0;
    for (int i = 0; i < ne; ++i)
      if (enemies[i] == g_marchTarget) idx = i;
    if (in->released & CP_BTN_RIGHT) {
      g_marchTarget = enemies[(idx + 1) % ne];
      return CP_LOOP_RENDER;
    }
    if (in->released & CP_BTN_LEFT) {
      g_marchTarget = enemies[(idx + ne - 1) % ne];
      return CP_LOOP_RENDER;
    }
    if (in->released & CP_BTN_CONFIRM) {
      const uint16_t send = (uint16_t)(st.cities[g_selCity].troops * 8 / 10);
      char msg[48];
      resolveBattle(api, g_selCity, g_marchTarget, send, msg, sizeof(msg));
      logAdd(msg);
      --st.actionsLeft;
      g_marchTarget = -1;
      g_actionSel = -1;
      checkOutcome();
      save(api);
      return CP_LOOP_RENDER;
    }
    return CP_LOOP_IDLE;
  }

  // Action submenu.
  if (g_actionSel >= 0) {
    if (app_menu_input(api, in, &g_actionSel, 5)) {
      if (g_actionSel == 4) {
        int enemies[4];
        if (adjacentEnemy(g_selCity, enemies, 4) > 0 && st.cities[g_selCity].troops >= 1000) g_marchTarget = enemies[0];
      } else {
        doAction(api, g_actionSel);
      }
      return CP_LOOP_RENDER;
    }
    return CP_LOOP_IDLE;
  }

  // Map cursor mode.
  uint32_t f = CP_LOOP_IDLE;
  if (in->released & CP_BTN_UP) {
    g_selCity = (g_selCity + SG_CITIES - 1) % SG_CITIES;
    f = CP_LOOP_RENDER;
  } else if (in->released & CP_BTN_DOWN) {
    g_selCity = (g_selCity + 1) % SG_CITIES;
    f = CP_LOOP_RENDER;
  } else if (in->released & CP_BTN_CONFIRM) {
    if (st.cities[g_selCity].owner == st.playerFaction) {
      g_actionSel = 0;
      f = CP_LOOP_RENDER;
    }
  } else if ((in->released & CP_BTN_LEFT) || (in->released & CP_BTN_RIGHT)) {
    endTurn(api);  // end the round
    f = CP_LOOP_RENDER;
  }
  if (f == CP_LOOP_IDLE) api->delay_ms(40);
  return f;
}

// ---- render ---------------------------------------------------------------
static void on_render(const CpApi* api) {
  char buf[64];
  const int w = api->screen_width();
  const int h = api->screen_height();
  api->clear_screen();

  if (g_view == V_MENU) {
    app_header(api, "三国霸业", "");
    static const char* full[4] = {"继续天下", "新的天下", "玩法说明", "关于"};
    app_menu_draw(api, 70, g_hasSave ? full : full + 1, g_hasSave ? 4 : 3, g_menuSel);
    app_hints(api, "返回", "选择", "上下移动", "");
    if (g_about.open) app_about_draw(api, "三国霸业");
    return;
  }
  if (g_view == V_FACTION) {
    app_header(api, "选择势力", "");
    static char lb[3][32];
    static const char* pt[3];
    for (int i = 0; i < 3; ++i) {
      cp_snprintf(lb[i], sizeof(lb[0]), "%s（%s）", kFactionNames[i], kFactionLeaders[i]);
      pt[i] = lb[i];
    }
    app_menu_draw(api, 70, pt, 3, g_menuSel);
    app_hints(api, "返回", "开始", "上下选择", "");
    return;
  }
  if (g_view == V_HELP) {
    app_header(api, "玩法说明", "");
    api->draw_text_wrapped(CP_FONT_UI, 20, 56, w - 40, 14, kHelp, 1, CP_TEXT_REGULAR);
    app_hints(api, "返回", "确定", "", "");
    return;
  }
  if (g_view == V_ABOUT_MENU) {
    app_header(api, "三国霸业", "");
    app_about_draw(api, "三国霸业");
    app_hints(api, "返回", "确定", "", "");
    return;
  }

  // ---- play view ----
  if (g_outcome) {
    app_header(api, "三国霸业", "");
    api->draw_text_centered(CP_FONT_TITLE, w / 2, h / 2 - 40, g_outcome == 1 ? "一统天下！" : "大势已去", 1,
                            CP_TEXT_BOLD);
    api->draw_text_centered(CP_FONT_UI, w / 2, h / 2 + 20, g_outcome == 1 ? "十二城尽归旗下" : "最后的城池已经陷落", 1,
                            CP_TEXT_REGULAR);
    app_hints(api, "返回", "确认返回", "", "");
    return;
  }

  const int pf = st.playerFaction;
  cp_snprintf(buf, sizeof(buf), "第%d回合·行动%d", st.round, st.actionsLeft);
  const int top = app_header(api, kFactionNames[pf], buf);

  cp_snprintf(buf, sizeof(buf), "金 %u  粮 %u  城 %d", st.gold[pf], st.food[pf], countCities(pf));
  api->draw_text(CP_FONT_UI, 14, top, buf, 1, CP_TEXT_REGULAR);

  // City list (left column).
  const int listY = top + 26;
  const int rowH = 22;
  for (int i = 0; i < SG_CITIES; ++i) {
    const City* c = &st.cities[i];
    const int y = listY + i * rowH;
    const int sel = (i == g_selCity);
    if (sel) api->fill_rect(10, y, w - 20, rowH, 1);
    const char* own = c->owner == kNeutral ? "群" : kFactionNames[c->owner];
    cp_snprintf(buf, sizeof(buf), "%s[%s] 兵%u 防%d", kCities[i].name, own, c->troops, c->walls);
    api->draw_text(CP_FONT_UI, 16, y + 3, buf, sel ? 0 : 1, c->owner == pf ? CP_TEXT_BOLD : CP_TEXT_REGULAR);
  }

  // Action submenu / march overlay drawn at the bottom.
  if (g_marchTarget >= 0) {
    api->fill_rect(0, h - 130, w, 130, 0);
    api->draw_rect(8, h - 126, w - 16, 118, 1);
    cp_snprintf(buf, sizeof(buf), "出征目标：%s（守军%u 防%d）", kCities[g_marchTarget].name,
                st.cities[g_marchTarget].troops, st.cities[g_marchTarget].walls);
    api->draw_text(CP_FONT_UI, 18, h - 118, buf, 1, CP_TEXT_BOLD);
    cp_snprintf(buf, sizeof(buf), "派兵 %u", (unsigned)(st.cities[g_selCity].troops * 8 / 10));
    api->draw_text(CP_FONT_UI, 18, h - 92, buf, 1, CP_TEXT_REGULAR);
    app_hints(api, "取消", "出征", "左右换目标", "");
  } else if (g_actionSel >= 0) {
    api->fill_rect(0, h - 130, w, 130, 0);
    api->draw_rect(8, h - 126, w - 16, 118, 1);
    for (int a = 0; a < 5; ++a) {
      const int x = 16 + (a % 3) * ((w - 32) / 3);
      const int y = h - 116 + (a / 3) * 40;
      const int sel = (a == g_actionSel);
      if (sel)
        api->fill_rect(x, y, (w - 32) / 3 - 6, 34, 1);
      else
        api->draw_rect(x, y, (w - 32) / 3 - 6, 34, 1);
      api->draw_text(CP_FONT_UI, x + 8, y + 8, kActions[a], sel ? 0 : 1, CP_TEXT_BOLD);
    }
    app_hints(api, "返回", "执行", "上下选动作", "");
  } else {
    // Log tail
    int ly = h - 96;
    for (int i = (g_logN > 3 ? g_logN - 3 : 0); i < g_logN; ++i) {
      api->draw_text(CP_FONT_SMALL, 14, ly, g_log[i], 1, CP_TEXT_REGULAR);
      ly += 16;
    }
    app_hints(api, "返回", "选城", "上下选城", "左右=结束回合");
  }
  if (g_about.open) app_about_draw(api, "三国霸业");
}

static void on_exit(const CpApi* api) {
  if (g_view == V_PLAY && !g_outcome) save(api);
}

static const CpApp kApp = {CP_ABI_VERSION, 0, "三国霸业", 10100, on_enter, on_loop, on_render, on_exit};
__attribute__((visibility("default"))) const CpApp* cp_app_entry(void) { return &kApp; }
