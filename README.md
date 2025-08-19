# TimeWallpaper v2.0 - Solar Edition

TimeWallpaper automatically changes your desktop wallpaper based on real astronomical data for your location.

## 🌟 Key Features

### Real Astronomical Data
- Seasonal Accuracy: Colors automatically adjust throughout the year as sunrise/sunset times shift
- Minute Precision: Subtle changes in color and shade every minute
- Location-Aware: Uses your exact coordinates for solar calculations
- Privacy Friendly: Uses external IP geolocation service (ip-api.com)
- Daily Updates: Automatically fetches fresh solar data each day
- Backup Coordinates: Manual coordinates serve as fallback if detection fails

## 🚀 Quick Start

A. Run Forever - Compile or move TimeWallpaper.exe here:
   ```
   %APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\
   ```
   Runs every time you log in.

B. Run Manually:
   ```
   cmd > TimeWallpaper.exe
   ```
   Runs continuously until you close it.

## ⚙️ Optional Configuration

Edit `config.ini` to customize:

- Disable/enable auto-detection to use manual coordinates
- Enter manual/backup coordinates (get from https://www.latlong.net/)
- Update frequency (minutes)
- Enable debug output

## 🔧 Manage

To uninstall, delete this file:
```
%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\TimeWallpaper_Startup.bat
```

Check if Running:
- Look for `TimeWallpaper.exe` in Task Manager
- Wallpaper should change colors gradually throughout the day

## 🌅 Color Schedule

Colors change based on your location's actual solar times:

- Deep Night → Nearly black midnight blue
- Pre-Dawn → Dark purple (3 hours before sunrise)
- Dawn → Purple-pink (30 minutes before sunrise) 
- Sunrise → Yellow-peach (actual sunrise time)
- Morning → Pale green (2 hours after sunrise)
- Solar Noon → Sky blue (actual solar noon)
- Afternoon → Light blue/golden
- Sunset → Orange (actual sunset time)
- Civil Twilight → Dark slate purple
- Evening → Deep purple
- Night → Nearly black

## 📊 Sample Output

```
TimeWallpaper v2.0 - Solar Edition
===================================
IP geolocation successful:
  Location: Rochester, New York
  Coordinates: 43.114, -77.5689
Location: Rochester, New York (43.114, -77.5689)
Update interval: 1 minute(s)
Solar times fetched successfully:
  Sunrise: 7:12 AM
  Solar Noon: 12:09 PM  
  Sunset: 5:06 PM
  Civil Twilight End: 5:36 PM

Wallpaper updated successfully!
Current time: 7:12 AM
Period: Sunrise
Color: RGB(255, 229, 204)
```
