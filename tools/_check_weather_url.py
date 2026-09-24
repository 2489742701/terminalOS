# -*- coding: utf-8 -*-
"""在 PC 上验证 open-meteo 的完整 URL（含 hourly + forecast_hours）是否 200，
并打印响应大小与 hourly.time 前几条，确认设备端解析逻辑拿到的结构一致。"""
import json, urllib.request, ssl

LA, LO = 32.0589, 118.7738
url = (
    "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
    "&current=temperature_2m,relative_humidity_2m,apparent_temperature,"
    "is_day,precipitation,weather_code,cloud_cover,pressure_msl,"
    "surface_pressure,wind_speed_10m,wind_direction_10m,wind_gusts_10m"
    "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
    "apparent_temperature_max,apparent_temperature_min,precipitation_sum,"
    "precipitation_probability_max,wind_speed_10m_max,wind_gusts_10m_max,"
    "uv_index_max,sunrise,sunset"
    "&hourly=temperature_2m,weather_code,precipitation_probability"
    "&timezone=auto&forecast_days=7&forecast_hours=24"
) % (LA, LO)

print("URL len =", len(url))
ctx = ssl.create_default_context()
req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
with urllib.request.urlopen(req, timeout=30, context=ctx) as r:
    body = r.read()
print("HTTP", r.status, len(body), "bytes")
j = json.loads(body)
print("hourly keys:", list(j.get("hourly", {}).keys()))
print("hourly.time count:", len(j.get("hourly", {}).get("time", [])))
print("first 5 time:", j["hourly"]["time"][:5])
print("daily.time:", j["daily"]["time"])
print("current.temperature_2m:", j["current"]["temperature_2m"])
