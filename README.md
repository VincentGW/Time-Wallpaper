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

1. Compile: `compile.bat`

2. Run Once (location auto-detected):
   cmd
   TimeWallpaper.exe
   
3. Run Continuously:
   cmd
   TimeWallpaper.exe -c
   
4. Install as Windows Service:
   cmd
   install_service.bat


## ⚙️ Optional Configuration

Edit `config.ini` to customize:

# Disable/enable auto-detection to use manual coordinates
# Enter manual/backup coordinates (get from https://www.latlong.net/)
# Update frequency (minutes)
# Enable debug output


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


## 🌍 Seasonal Examples

New York City:
- Winter: Sunrise ~7:12 AM, Sunset ~5:06 PM
- Summer: Sunrise ~5:30 AM, Sunset ~8:00 PM

London:  
- Winter: Sunrise ~8:00 AM, Sunset ~4:00 PM
- Summer: Sunrise ~4:45 AM, Sunset ~9:20 PM

Colors automatically adapt to your location's natural light cycle!