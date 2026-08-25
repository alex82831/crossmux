#pragma once

// Built-in forecast locations for the Weather app: municipalities, provincial
// capitals, and major cities, ordered so the most common picks come first.
// Flash-resident (constexpr); glyph coverage for every name is guaranteed by
// lib/EpdFont/scripts/cn_apps_chars.txt (see chinese-build.md).
struct WeatherCity {
  const char* name;
  float lat;
  float lon;
};

constexpr WeatherCity kWeatherCities[] = {
    {"北京", 39.90f, 116.41f},     {"上海", 31.23f, 121.47f},    {"广州", 23.13f, 113.26f}, {"深圳", 22.55f, 114.06f},
    {"成都", 30.57f, 104.07f},     {"杭州", 30.27f, 120.16f},    {"武汉", 30.59f, 114.31f}, {"西安", 34.34f, 108.94f},
    {"重庆", 29.56f, 106.55f},     {"南京", 32.06f, 118.80f},    {"天津", 39.13f, 117.20f}, {"苏州", 31.30f, 120.62f},
    {"郑州", 34.75f, 113.62f},     {"长沙", 28.23f, 112.94f},    {"沈阳", 41.80f, 123.43f}, {"青岛", 36.07f, 120.38f},
    {"大连", 38.91f, 121.61f},     {"宁波", 29.87f, 121.55f},    {"厦门", 24.48f, 118.09f}, {"福州", 26.07f, 119.30f},
    {"济南", 36.65f, 117.12f},     {"合肥", 31.82f, 117.23f},    {"昆明", 24.88f, 102.83f}, {"哈尔滨", 45.80f, 126.53f},
    {"长春", 43.90f, 125.33f},     {"石家庄", 38.04f, 114.51f},  {"太原", 37.87f, 112.55f}, {"南昌", 28.68f, 115.86f},
    {"贵阳", 26.65f, 106.63f},     {"兰州", 36.06f, 103.83f},    {"南宁", 22.82f, 108.37f}, {"海口", 20.04f, 110.32f},
    {"呼和浩特", 40.84f, 111.75f}, {"乌鲁木齐", 43.83f, 87.62f}, {"拉萨", 29.65f, 91.14f},  {"西宁", 36.62f, 101.78f},
    {"银川", 38.49f, 106.23f},     {"香港", 22.32f, 114.17f},    {"澳门", 22.20f, 113.55f}, {"台北", 25.03f, 121.57f},
};

constexpr int kWeatherCityCount = static_cast<int>(sizeof(kWeatherCities) / sizeof(kWeatherCities[0]));
