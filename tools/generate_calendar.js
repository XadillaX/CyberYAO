#!/usr/bin/env node
"use strict";

const fs = require("fs");
const { Solar } = require("lunar-javascript");

const output = process.argv[2];
if (!output) throw new Error("缺少输出文件路径");

const START_YEAR = 2024;
const END_YEAR = 2040;
const GAN_ZHI = Array.from({ length: 60 }, (_, index) => {
  const gan = "甲乙丙丁戊己庚辛壬癸";
  const zhi = "子丑寅卯辰巳午未申酉戌亥";
  return gan[index % 10] + zhi[index % 12];
});
const MONTHS = [
  "",
  "正月",
  "二月",
  "三月",
  "四月",
  "五月",
  "六月",
  "七月",
  "八月",
  "九月",
  "十月",
  "冬月",
  "腊月",
];
const DAYS = [
  "",
  "初一",
  "初二",
  "初三",
  "初四",
  "初五",
  "初六",
  "初七",
  "初八",
  "初九",
  "初十",
  "十一",
  "十二",
  "十三",
  "十四",
  "十五",
  "十六",
  "十七",
  "十八",
  "十九",
  "二十",
  "廿一",
  "廿二",
  "廿三",
  "廿四",
  "廿五",
  "廿六",
  "廿七",
  "廿八",
  "廿九",
  "三十",
];

function pool() {
  const values = [];
  const ids = new Map();
  return {
    id(value) {
      if (!ids.has(value)) {
        ids.set(value, values.length);
        values.push(value);
      }
      return ids.get(value);
    },
    values,
  };
}

function cString(value) {
  return `"${value.replaceAll("\\", "\\\\").replaceAll('"', '\\"')}"`;
}

const yiPool = pool();
const jiPool = pool();
const termPool = pool();
const records = [];

for (let year = START_YEAR; year <= END_YEAR; year++) {
  for (let month = 1; month <= 12; month++) {
    const days = new Date(Date.UTC(year, month, 0)).getUTCDate();
    for (let day = 1; day <= days; day++) {
      const lunar = Solar.fromYmd(year, month, day).getLunar();
      const lunarMonth = lunar.getMonth();
      const previousTerm = lunar.getPrevJieQi();
      const term = previousTerm ? previousTerm.getName() : "";
      records.push([
        GAN_ZHI.indexOf(lunar.getYearInGanZhi()),
        Math.abs(lunarMonth),
        lunarMonth < 0 ? 1 : 0,
        lunar.getDay(),
        GAN_ZHI.indexOf(lunar.getMonthInGanZhi()),
        GAN_ZHI.indexOf(lunar.getDayInGanZhi()),
        termPool.id(term),
        yiPool.id(lunar.getDayYi().slice(0, 2).join(" · ")),
        jiPool.id(lunar.getDayJi().slice(0, 2).join(" · ")),
      ]);
    }
  }
}

function emitStrings(name, values) {
  return `static const char* const ${name}[] = {\n${values
    .map((value) => `    ${cString(value)},`)
    .join("\n")}\n};\n\n`;
}

let source =
  '#include "yaogui_calendar.h"\n\n' +
  "#include <stddef.h>\n" +
  "#include <stdint.h>\n" +
  "#include <stdio.h>\n\n";
source += `#define CALENDAR_START_YEAR ${START_YEAR}\n`;
source += `#define CALENDAR_END_YEAR ${END_YEAR}\n\n`;
source +=
  "typedef struct __attribute__((packed)) {\n" +
  "  uint8_t lunar_year_gz;\n" +
  "  uint8_t lunar_month;\n" +
  "  uint8_t leap_month;\n" +
  "  uint8_t lunar_day;\n" +
  "  uint8_t month_gz;\n" +
  "  uint8_t day_gz;\n" +
  "  uint8_t term;\n" +
  "  uint16_t yi;\n" +
  "  uint16_t ji;\n" +
  "} calendar_record_t;\n\n";
source += emitStrings("GAN_ZHI", GAN_ZHI);
source += emitStrings("LUNAR_MONTHS", MONTHS);
source += emitStrings("LUNAR_DAYS", DAYS);
source += emitStrings("SOLAR_TERMS", termPool.values);
source += emitStrings("DAY_YI", yiPool.values);
source += emitStrings("DAY_JI", jiPool.values);
source += "static const calendar_record_t RECORDS[] = {\n";
source += records
  .map(
    (record) =>
      `    {${record[0]}, ${record[1]}, ${record[2]}, ${record[3]}, ` +
      `${record[4]}, ${record[5]}, ${record[6]}, ${record[7]}, ${record[8]}},`,
  )
  .join("\n");
source += "\n};\n\n";
source +=
  "static bool leap_year(int year) {\n" +
  "  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);\n" +
  "}\n\n" +
  "static int day_index(int year, int month, int day) {\n" +
  "  static const uint16_t before_month[] = {\n" +
  "      0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334,\n" +
  "  };\n" +
  "  if (year < CALENDAR_START_YEAR || year > CALENDAR_END_YEAR ||\n" +
  "      month < 1 || month > 12 || day < 1 || day > 31) return -1;\n" +
  "  int index = 0;\n" +
  "  for (int value = CALENDAR_START_YEAR; value < year; value++)\n" +
  "    index += leap_year(value) ? 366 : 365;\n" +
  "  index += before_month[month] + day - 1;\n" +
  "  if (month > 2 && leap_year(year)) index++;\n" +
  "  return index;\n" +
  "}\n\n" +
  "bool yaogui_calendar_lookup(int year, int month, int day,\n" +
  "                            yaogui_calendar_day_t* result) {\n" +
  "  if (!result) return false;\n" +
  "  const int index = day_index(year, month, day);\n" +
  "  if (index < 0 || (size_t)index >= sizeof(RECORDS) / sizeof(RECORDS[0]))\n" +
  "    return false;\n" +
  "  const calendar_record_t* record = &RECORDS[index];\n" +
  "  snprintf(result->lunar, sizeof(result->lunar), \"农历%s年%s%s%s\",\n" +
  "           GAN_ZHI[record->lunar_year_gz], record->leap_month ? \"闰\" : \"\",\n" +
  "           LUNAR_MONTHS[record->lunar_month], LUNAR_DAYS[record->lunar_day]);\n" +
  "  snprintf(result->ganzhi, sizeof(result->ganzhi), \"%s月  %s日  %s\",\n" +
  "           GAN_ZHI[record->month_gz], GAN_ZHI[record->day_gz],\n" +
  "           SOLAR_TERMS[record->term]);\n" +
  "  result->yi = DAY_YI[record->yi];\n" +
  "  result->ji = DAY_JI[record->ji];\n" +
  "  return true;\n" +
  "}\n";

fs.writeFileSync(output, source);
