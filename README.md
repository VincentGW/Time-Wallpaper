# TimeWallpaper v2.0 - Solar Edition

TimeWallpaper automatically changes your desktop wallpaper based on real astronomical data for your location.

## 🚀 Quick Start

To run every time you login - Compile or move TimeWallpaper.exe here:
   ```
   %APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\
   ```

## 🌟 Key Features

### Real Astronomical Data
- Seasonal Accuracy: Colors automatically adjust throughout the year as sunrise/sunset times shift
- Location-Sensitive: Uses your exact coordinates for solar calculations
- Quarter Minute Precision: Subtle changes in color and shade every quarter minute
- Regular Schedule: Automatically fetches fresh solar data each day
- Backup Coordinates: Manual coordinates serve as fallback if detection fails

## 🌅 Color Schedule

Colors change based on your location's actual solar times:

- Deep Night → Nearly black midnight blue
- Pre-Dawn → Dark purple
- Dawn → Purple-pink
- Sunrise → Yellow-peach
- Morning → Pale green
- Solar Noon → Sky blue
- Afternoon → Light blue/golden
- Sunset → Orange
- Civil Twilight → Dark slate purple
- Evening → Deep purple
- Night → Nearly black

## ⚙️ Optional Configuration

Edit `config.ini` to customize:

- Disable/enable auto-detection to use manual coordinates
- Enter manual/backup coordinates (get from https://www.latlong.net/)
- Enable debug output

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
