#pragma once

#include <stdint.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * IP 定位（给天气用经纬度 + 中文城市名）
 *
 * 主源：ip-api.com —— 105 B，支持 lang=zh-CN 直接返回中文城市，免 key。
 *    GET http://ip-api.com/json/?fields=status,country,regionName,city,lat,lon&lang=zh-CN
 *    -> {"status":"success","country":"中国","regionName":"江苏","city":"南京",
 *        "lat":32.0607,"lon":118.763}
 * 备源：api.ip.sb/geoip（HTTPS，367 B，字段是 latitude/longitude，城市名是英文）
 *
 * 2026-09-24 实测：两个源在本机都是一次就连上（不像 uapis.cn 会 DNS 失败）。
 * ⚠️ 这个网络的 DNS 对某些域名会**偶发**失败（uapis.cn 第一次必失败、第二次成），
 *    所以 locate() 内部带一轮重试，别把一次失败当"接口不能用"。
 *
 * 结果存 NVS（命名空间 "geoip"），开机直接读缓存，不用每次联网。
 * ⚠️ locate() 是**阻塞**的（含 HTTP），只在后台任务 / 串口命令里调。
 * ═══════════════════════════════════════════════════════════════════════════ */

namespace GeoIP {

// 定位。force=true 忽略缓存重新定位（默认只在缓存为空/过期时联网）。成功返回 true。
bool locate(bool force = false);

// 缓存里有没有可用坐标
bool valid();

const char* city();     // 中文城市名（缓存没内容时是 "北京"）
const char* region();   // 中文省/州
double lat();
double lon();

// 设备侧实测各反向 geocoding 源（选源用，串口 geotest）
void probe();

// 缓存年龄（秒）。0xFFFFFFFF = 没有缓存
uint32_t ageSec();

// 清缓存（下次 locate 强制联网）
void reset();

}  // namespace GeoIP
