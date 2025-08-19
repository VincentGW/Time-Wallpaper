#include <Windows.h>
#include <iostream>
#include <ctime>
#include <string>
#include <fstream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <thread>
#include <iomanip>
#include <sstream>
#include <wininet.h>
#include <comdef.h>
#include <combaseapi.h>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "ole32.lib")

// Windows Service globals
SERVICE_STATUS g_ServiceStatus = { 0 };
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE g_ServiceStopEvent = INVALID_HANDLE_VALUE;

struct Color {
    int r, g, b;
    Color(int red = 0, int green = 0, int blue = 0) : r(red), g(green), b(blue) {}
};

struct SolarTimes {
    double sunrise_hour;
    double sunset_hour;
    double solar_noon_hour;
    double civil_twilight_begin;
    double civil_twilight_end;
    bool valid = false;
    std::string fetch_date;
};

struct Config {
    double latitude = 40.7128;   // Default: NYC
    double longitude = -74.0060;
    std::string location_name = "New York City";
    int update_interval_minutes = 1;
    bool service_mode = false;
    bool debug_mode = false;
    bool auto_detect_location = true;
};

class TimeWallpaper {
private:
    int screenWidth, screenHeight;
    std::string tempPath;
    Config config;
    
    struct ColorPoint {
        double hour;
        Color color;
        std::string period;
    };
    
    std::vector<ColorPoint> todaysColors;
    SolarTimes todaysSolarTimes;
    std::string lastFetchDate;

public:
    TimeWallpaper() {
        screenWidth = GetSystemMetrics(SM_CXSCREEN);
        screenHeight = GetSystemMetrics(SM_CYSCREEN);
        
        char tempDir[MAX_PATH];
        GetTempPathA(MAX_PATH, tempDir);
        tempPath = std::string(tempDir) + "time_wallpaper.bmp";
        
        loadConfig();
        
        // Don't auto-detect location in constructor for service mode - do it later in run()
        if (config.auto_detect_location && !config.service_mode) {
            autoDetectLocation();
        }
        
        if (!config.service_mode) {
            logMessage("TimeWallpaper v2.0 - Solar Edition");
            logMessage("===================================");
            logMessage("Location: " + config.location_name + " (" + std::to_string(config.latitude) + ", " + std::to_string(config.longitude) + ")");
            logMessage("Update interval: " + std::to_string(config.update_interval_minutes) + " minute(s)");
            logMessage("Screen: " + std::to_string(screenWidth) + "x" + std::to_string(screenHeight));
        }
    }
    
    std::string getConfigPath() {
        // Get the directory where the executable is located
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        std::string exeDir = std::string(exePath);
        size_t lastSlash = exeDir.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            exeDir = exeDir.substr(0, lastSlash + 1);
        } else {
            exeDir = "";
        }
        
        // Go up one directory level
        size_t parentSlash = exeDir.find_last_of("\\/", exeDir.length() - 2);
        std::string parentDir;
        if (parentSlash != std::string::npos) {
            parentDir = exeDir.substr(0, parentSlash + 1);
        } else {
            parentDir = exeDir; // Fallback to current directory if parent not found
        }
        
        return parentDir + "config.ini";
    }
    
    void loadConfig() {
        std::string configPath = getConfigPath();
        std::ifstream configFile(configPath);
        if (configFile.is_open()) {
            std::string line;
            while (std::getline(configFile, line)) {
                if (line.empty() || line[0] == '#') continue;
                
                size_t equalPos = line.find('=');
                if (equalPos != std::string::npos) {
                    std::string key = line.substr(0, equalPos);
                    std::string value = line.substr(equalPos + 1);
                    
                    // Trim whitespace
                    key.erase(0, key.find_first_not_of(" \t"));
                    key.erase(key.find_last_not_of(" \t") + 1);
                    value.erase(0, value.find_first_not_of(" \t"));
                    value.erase(value.find_last_not_of(" \t") + 1);
                    
                    if (key == "latitude") config.latitude = std::stod(value);
                    else if (key == "longitude") config.longitude = std::stod(value);
                    else if (key == "location_name") config.location_name = value;
                    else if (key == "update_interval_minutes") config.update_interval_minutes = std::stoi(value);
                    else if (key == "service_mode") config.service_mode = (value == "true");
                    else if (key == "debug_mode") config.debug_mode = (value == "true");
                    else if (key == "auto_detect_location") config.auto_detect_location = (value == "true");
                }
            }
            configFile.close();
            if (config.debug_mode) logMessage("Config loaded from config.ini");
        } else {
            createDefaultConfig();
        }
    }
    
    void createDefaultConfig() {
        std::string configPath = getConfigPath();
        std::ofstream configFile(configPath);
        if (configFile.is_open()) {
            configFile << "# TimeWallpaper Configuration" << std::endl;
            configFile << "# auto_detect_location=true: Automatically detect your location via IP geolocation" << std::endl;
            configFile << "# auto_detect_location=false: Use manual coordinates below as primary location" << std::endl;
            configFile << "# Manual coordinates also serve as backup if auto-detection fails" << std::endl;
            configFile << "# Get coordinates from: https://www.latlong.net/" << std::endl;
            configFile << std::endl;
            configFile << "latitude=" << config.latitude << std::endl;
            configFile << "longitude=" << config.longitude << std::endl;
            configFile << "location_name=" << config.location_name << std::endl;
            configFile << "update_interval_minutes=" << config.update_interval_minutes << std::endl;
            configFile << "service_mode=" << (config.service_mode ? "true" : "false") << std::endl;
            configFile << "debug_mode=" << (config.debug_mode ? "true" : "false") << std::endl;
            configFile << "auto_detect_location=" << (config.auto_detect_location ? "true" : "false") << std::endl;
            configFile.close();
            logMessage("Created default config.ini - location will be auto-detected!");
        }
    }
    
    std::string httpGetWithTimeout(const std::string& url, int timeoutMs = 5000) {
        HINTERNET hInternet = InternetOpenA("TimeWallpaper/2.0", INTERNET_OPEN_TYPE_DIRECT, nullptr, nullptr, 0);
        if (!hInternet) return "";
        
        // Set timeouts
        DWORD timeout = timeoutMs;
        InternetSetOptionA(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionA(hInternet, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionA(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
        
        HINTERNET hUrl = InternetOpenUrlA(hInternet, url.c_str(), nullptr, 0, 
                                         INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
        if (!hUrl) {
            InternetCloseHandle(hInternet);
            return "";
        }
        
        std::string response;
        char buffer[4096];
        DWORD bytesRead;
        
        while (InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
            response.append(buffer, bytesRead);
        }
        
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);
        return response;
    }
    
    bool detectLocationFromIP() {
        if (config.debug_mode) logMessage("Trying IP geolocation...");
        
        std::string response = httpGetWithTimeout("http://ip-api.com/json/?fields=status,lat,lon,city,regionName,country", 8000);
        
        if (response.empty()) {
            if (config.debug_mode) logMessage("IP geolocation failed - no response");
            return false;
        }
        
        if (response.find("\"status\":\"success\"") == std::string::npos) {
            if (config.debug_mode) logMessage("IP geolocation failed - invalid response");
            return false;
        }
        
        try {
            // Parse latitude
            size_t latPos = response.find("\"lat\":");
            if (latPos != std::string::npos) {
                size_t startPos = latPos + 6;
                size_t endPos = response.find_first_of(",}", startPos);
                if (endPos != std::string::npos) {
                    std::string latStr = response.substr(startPos, endPos - startPos);
                    config.latitude = std::stod(latStr);
                }
            }
            
            // Parse longitude
            size_t lonPos = response.find("\"lon\":");
            if (lonPos != std::string::npos) {
                size_t startPos = lonPos + 6;
                size_t endPos = response.find_first_of(",}", startPos);
                if (endPos != std::string::npos) {
                    std::string lonStr = response.substr(startPos, endPos - startPos);
                    config.longitude = std::stod(lonStr);
                }
            }
            
            // Parse city name
            size_t cityPos = response.find("\"city\":\"");
            if (cityPos != std::string::npos) {
                size_t startPos = cityPos + 8;
                size_t endPos = response.find("\"", startPos);
                if (endPos != std::string::npos) {
                    std::string city = response.substr(startPos, endPos - startPos);
                    
                    // Also get region for better location name
                    size_t regionPos = response.find("\"regionName\":\"");
                    if (regionPos != std::string::npos) {
                        size_t regionStart = regionPos + 14;
                        size_t regionEnd = response.find("\"", regionStart);
                        if (regionEnd != std::string::npos) {
                            std::string region = response.substr(regionStart, regionEnd - regionStart);
                            config.location_name = city + ", " + region;
                        } else {
                            config.location_name = city;
                        }
                    } else {
                        config.location_name = city;
                    }
                }
            }
            
            if (config.debug_mode) {
                logMessage("IP geolocation successful:");
                logMessage("  Location: " + config.location_name);
                logMessage("  Coordinates: " + std::to_string(config.latitude) + ", " + std::to_string(config.longitude));
            }
            
            return true;
            
        } catch (...) {
            if (config.debug_mode) logMessage("IP geolocation failed - parsing error");
            return false;
        }
    }
    
    bool autoDetectLocation() {
        if (!config.auto_detect_location) {
            if (config.debug_mode) logMessage("Auto-detection disabled in config");
            return false;
        }
        
        if (config.debug_mode) logMessage("Auto-detecting location...");
        
        // Try IP geolocation first (most reliable)
        if (detectLocationFromIP()) {
            saveLocationToConfig();
            return true;
        }
        
        if (config.debug_mode) logMessage("All location detection methods failed");
        return false;
    }
    
    void saveLocationToConfig() {
        // Update the config file with detected location
        std::string configPath = getConfigPath();
        std::ofstream configFile(configPath);
        if (configFile.is_open()) {
            configFile << "# TimeWallpaper Configuration" << std::endl;
            configFile << "# Location auto-detected successfully!" << std::endl;
            configFile << "# Set auto_detect_location=false to use manual coordinates as primary location." << std::endl;
            configFile << "# Manual coordinates below serve as backup if auto-detection fails." << std::endl;
            configFile << std::endl;
            configFile << "latitude=" << config.latitude << std::endl;
            configFile << "longitude=" << config.longitude << std::endl;
            configFile << "location_name=" << config.location_name << std::endl;
            configFile << "update_interval_minutes=" << config.update_interval_minutes << std::endl;
            configFile << "service_mode=" << (config.service_mode ? "true" : "false") << std::endl;
            configFile << "debug_mode=" << (config.debug_mode ? "true" : "false") << std::endl;
            configFile << "auto_detect_location=" << (config.auto_detect_location ? "true" : "false") << std::endl;
            configFile.close();
            
            if (config.debug_mode) logMessage("Location saved to config.ini");
        }
    }
    
    double parseTimeString(const std::string& timeStr) {
        if (timeStr.length() < 19) return 0.0;
        
        std::string timeOnly = timeStr.substr(11, 8);
        int hours, minutes, seconds;
        char colon1, colon2;
        
        std::stringstream ss(timeOnly);
        if (ss >> hours >> colon1 >> minutes >> colon2 >> seconds) {
            double utcHour = hours + (minutes / 60.0) + (seconds / 3600.0);
            
            // Simple EST conversion (UTC-5) - TODO: proper timezone handling
            double localHour = utcHour - 5.0;
            if (localHour < 0) localHour += 24.0;
            if (localHour >= 24) localHour -= 24.0;
            
            return localHour;
        }
        
        return 0.0;
    }
    
    double parseTimeFromJson(const std::string& json, const std::string& key) {
        size_t keyPos = json.find(key);
        if (keyPos == std::string::npos) return 0.0;
        
        size_t startQuote = json.find("\"", keyPos + key.length());
        if (startQuote == std::string::npos) return 0.0;
        startQuote++;
        
        size_t endQuote = json.find("\"", startQuote);
        if (endQuote == std::string::npos) return 0.0;
        
        std::string timeStr = json.substr(startQuote, endQuote - startQuote);
        return parseTimeString(timeStr);
    }
    
    bool fetchSolarTimes(bool isRetry = false) {
        std::string today = getCurrentDate();
        if (todaysSolarTimes.valid && todaysSolarTimes.fetch_date == today) {
            if (config.debug_mode) logMessage("Using cached solar times for " + today);
            return true;
        }
        
        std::stringstream urlBuilder;
        urlBuilder << "https://api.sunrise-sunset.org/json?lat=" << config.latitude 
                   << "&lng=" << config.longitude << "&formatted=0";
        
        std::string url = urlBuilder.str();
        if (config.debug_mode) logMessage("Fetching solar times..." + std::string(isRetry ? " (retry)" : ""));
        
        std::string response = httpGetWithTimeout(url, isRetry ? 10000 : 5000);
        
        if (response.empty()) {
            if (!isRetry) {
                logMessage("API request timed out, retrying once...");
                return fetchSolarTimes(true);  // Retry once
            }
            logMessage("API Error: Request timed out. Using fallback times.");
            return useFallbackSolarTimes();
        }
        
        if (response.find("\"status\":\"OK\"") == std::string::npos) {
            logMessage("API Error: Invalid response. Using fallback times.");
            return useFallbackSolarTimes();
        }
        
        try {
            todaysSolarTimes.sunrise_hour = parseTimeFromJson(response, "\"sunrise\":");
            todaysSolarTimes.sunset_hour = parseTimeFromJson(response, "\"sunset\":");
            todaysSolarTimes.solar_noon_hour = parseTimeFromJson(response, "\"solar_noon\":");
            todaysSolarTimes.civil_twilight_begin = parseTimeFromJson(response, "\"civil_twilight_begin\":");
            todaysSolarTimes.civil_twilight_end = parseTimeFromJson(response, "\"civil_twilight_end\":");
            
            if (todaysSolarTimes.sunrise_hour > 0 && todaysSolarTimes.sunset_hour > 0) {
                todaysSolarTimes.valid = true;
                todaysSolarTimes.fetch_date = today;
                
                if (config.debug_mode) {
                    logMessage("Solar times fetched successfully:");
                    logMessage("  Sunrise: " + formatHour(todaysSolarTimes.sunrise_hour));
                    logMessage("  Sunset: " + formatHour(todaysSolarTimes.sunset_hour));
                }
                
                return true;
            }
        } catch (...) {
            logMessage("JSON parsing error. Using fallback times.");
        }
        
        return useFallbackSolarTimes();
    }
    
    bool useFallbackSolarTimes() {
        todaysSolarTimes.sunrise_hour = 7.2;
        todaysSolarTimes.sunset_hour = 17.1;
        todaysSolarTimes.solar_noon_hour = 12.15;
        todaysSolarTimes.civil_twilight_begin = 6.7;
        todaysSolarTimes.civil_twilight_end = 17.6;
        todaysSolarTimes.valid = false;
        todaysSolarTimes.fetch_date = getCurrentDate();
        
        logMessage("Using fallback solar times (NYC January average)");
        return false;
    }
    
    std::string getCurrentDate() {
        time_t now = time(0);
        tm* timeinfo = localtime(&now);
        
        std::stringstream ss;
        ss << (timeinfo->tm_year + 1900) << "-" 
           << std::setfill('0') << std::setw(2) << (timeinfo->tm_mon + 1) << "-"
           << std::setw(2) << timeinfo->tm_mday;
        return ss.str();
    }
    
    void generateTodaysColors() {
        todaysColors.clear();
        
        double sunrise = todaysSolarTimes.sunrise_hour;
        double sunset = todaysSolarTimes.sunset_hour;
        double solar_noon = todaysSolarTimes.solar_noon_hour;
        double civil_twilight_end = todaysSolarTimes.civil_twilight_end;
        
        std::vector<ColorPoint> points = {
            {0.0,                        Color(8, 8, 25),       "Deep Night"},
            {sunrise - 3.0,              Color(15, 10, 35),     "Pre-Dawn"}, 
            {sunrise - 1.0,              Color(25, 15, 45),     "Early Dawn"},
            {sunrise - 0.5,              Color(60, 30, 80),     "Dawn"},
            {sunrise,                    Color(255, 229, 204),  "Sunrise"},
            {sunrise + 0.5,              Color(255, 218, 185),  "Early Morning"},
            {sunrise + 2.0,              Color(230, 255, 230),  "Morning"},
            {solar_noon - 1.0,           Color(215, 255, 215),  "Late Morning"},
            {solar_noon,                 Color(135, 206, 250),  "Noon"},
            {solar_noon + 1.0,           Color(173, 216, 230),  "Early Afternoon"},
            {sunset - 2.0,               Color(210, 180, 140),  "Late Afternoon"},
            {sunset - 0.5,               Color(255, 165, 0),    "Pre-Sunset"},
            {sunset,                     Color(255, 140, 0),    "Sunset"},
            {sunset + 0.5,               Color(200, 80, 60),    "Post-Sunset"},
            {civil_twilight_end,         Color(72, 61, 85),     "Civil Twilight"},
            {civil_twilight_end + 1.0,   Color(65, 55, 80),     "Early Evening"},
            {civil_twilight_end + 2.0,   Color(45, 25, 65),     "Evening"},
            {civil_twilight_end + 3.0,   Color(25, 15, 45),     "Late Evening"},
            {23.99,                      Color(8, 8, 25),       "Night"}
        };
        
        // Fix times outside 0-24 range
        for (auto& point : points) {
            while (point.hour < 0) point.hour += 24.0;
            while (point.hour >= 24) point.hour -= 24.0;
        }
        
        std::sort(points.begin(), points.end(), 
                  [](const ColorPoint& a, const ColorPoint& b) {
                      return a.hour < b.hour;
                  });
        
        todaysColors = points;
        
        if (config.debug_mode) {
            logMessage("Generated " + std::to_string(todaysColors.size()) + " color points for today");
        }
    }
    
    Color getCurrentColor() {
        time_t now = time(0);
        tm* timeinfo = localtime(&now);
        double currentHour = timeinfo->tm_hour + (timeinfo->tm_min / 60.0);
        
        return getColorForHour(currentHour);
    }
    
    
    Color getColorForHour(double hour) {
        if (todaysColors.empty()) return Color(128, 128, 128);
        
        for (size_t i = 0; i < todaysColors.size() - 1; i++) {
            if (hour >= todaysColors[i].hour && hour <= todaysColors[i + 1].hour) {
                double progress = (hour - todaysColors[i].hour) / (todaysColors[i + 1].hour - todaysColors[i].hour);
                return interpolateColor(todaysColors[i].color, todaysColors[i + 1].color, progress);
            }
        }
        
        // Handle wrap-around
        double lastHour = todaysColors.back().hour;
        double firstHour = todaysColors.front().hour + 24;
        
        if (hour >= lastHour) {
            double progress = (hour - lastHour) / (firstHour - lastHour);
            return interpolateColor(todaysColors.back().color, todaysColors.front().color, progress);
        }
        
        return todaysColors.front().color;
    }
    
    std::string getCurrentPeriod() {
        time_t now = time(0);
        tm* timeinfo = localtime(&now);
        double currentHour = timeinfo->tm_hour + (timeinfo->tm_min / 60.0);
        
        for (size_t i = 0; i < todaysColors.size() - 1; i++) {
            if (currentHour >= todaysColors[i].hour && currentHour <= todaysColors[i + 1].hour) {
                return todaysColors[i].period;
            }
        }
        
        return todaysColors.back().period;
    }
    
    Color interpolateColor(Color start, Color end, double ratio) {
        ratio = std::max(0.0, std::min(1.0, ratio));
        
        int r = static_cast<int>(start.r * (1 - ratio) + end.r * ratio);
        int g = static_cast<int>(start.g * (1 - ratio) + end.g * ratio);
        int b = static_cast<int>(start.b * (1 - ratio) + end.b * ratio);
        
        return Color(r, g, b);
    }
    
    std::string formatHour(double hour) {
        int h = static_cast<int>(hour);
        int m = static_cast<int>((hour - h) * 60);
        
        std::string ampm = "AM";
        if (h == 0) h = 12;
        else if (h == 12) ampm = "PM";
        else if (h > 12) { h -= 12; ampm = "PM"; }
        
        std::stringstream ss;
        ss << h << ":" << std::setfill('0') << std::setw(2) << m << " " << ampm;
        return ss.str();
    }
    
    bool createSolidColorBitmap(Color color) {
        BITMAPFILEHEADER fileHeader = {};
        fileHeader.bfType = 0x4D42;
        fileHeader.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + (screenWidth * screenHeight * 3);
        fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
        
        BITMAPINFOHEADER infoHeader = {};
        infoHeader.biSize = sizeof(BITMAPINFOHEADER);
        infoHeader.biWidth = screenWidth;
        infoHeader.biHeight = screenHeight;
        infoHeader.biPlanes = 1;
        infoHeader.biBitCount = 24;
        infoHeader.biCompression = BI_RGB;
        infoHeader.biSizeImage = screenWidth * screenHeight * 3;
        
        std::vector<unsigned char> pixels(screenWidth * screenHeight * 3);
        for (int i = 0; i < screenWidth * screenHeight; i++) {
            pixels[i * 3] = color.b;
            pixels[i * 3 + 1] = color.g;
            pixels[i * 3 + 2] = color.r;
        }
        
        std::ofstream file(tempPath, std::ios::binary);
        if (!file) return false;
        
        file.write(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader));
        file.write(reinterpret_cast<char*>(&infoHeader), sizeof(infoHeader));
        file.write(reinterpret_cast<char*>(pixels.data()), pixels.size());
        
        file.close();
        return true;
    }
    
    bool setWallpaper() {
        int wideSize = MultiByteToWideChar(CP_UTF8, 0, tempPath.c_str(), -1, nullptr, 0);
        std::wstring widePath(wideSize, 0);
        MultiByteToWideChar(CP_UTF8, 0, tempPath.c_str(), -1, &widePath[0], wideSize);
        
        BOOL result = SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (PVOID)widePath.c_str(), 0);
        return result == TRUE;
    }
    
    DWORD getCurrentTaskbarColorSetting() {
        HKEY hKey;
        DWORD colorPrevalence = 0;
        DWORD dataSize = sizeof(DWORD);
        
        if (RegOpenKeyExA(HKEY_CURRENT_USER, 
                         "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 
                         0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            RegQueryValueExA(hKey, "ColorPrevalence", NULL, NULL, (LPBYTE)&colorPrevalence, &dataSize);
            RegCloseKey(hKey);
        }
        
        return colorPrevalence;
    }
    
    bool updateAccentColor(Color color) {
        DWORD currentTaskbarSetting = getCurrentTaskbarColorSetting();
        
        DWORD accentColor = (0xFF000000) | (color.r << 16) | (color.g << 8) | color.b;
        
        HKEY hKey;
        bool success = true;
        
        if (RegOpenKeyExA(HKEY_CURRENT_USER, 
                         "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 
                         0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            
            if (RegSetValueExA(hKey, "AccentColor", 0, REG_DWORD, 
                              (const BYTE*)&accentColor, sizeof(accentColor)) != ERROR_SUCCESS) {
                success = false;
            }
            
            if (RegSetValueExA(hKey, "ColorPrevalence", 0, REG_DWORD, 
                              (const BYTE*)&currentTaskbarSetting, sizeof(currentTaskbarSetting)) != ERROR_SUCCESS) {
                success = false;
            }
            
            RegCloseKey(hKey);
        } else {
            success = false;
        }
        
        if (success) {
            SendMessageTimeoutA(HWND_BROADCAST, WM_SETTINGCHANGE, 0, 
                               (LPARAM)"ImmersiveColorSet", SMTO_ABORTIFHUNG, 1000, NULL);
        }
        
        return success;
    }
    
    bool updateWallpaper() {
        Color currentColor = getCurrentColor();
        
        if (!createSolidColorBitmap(currentColor)) {
            logMessage("Failed to create wallpaper bitmap");
            return false;
        }
        
        if (!setWallpaper()) {
            logMessage("Failed to set wallpaper");
            return false;
        }
        
        if (!updateAccentColor(currentColor)) {
            logMessage("Failed to update accent color");
        }
        
        return true;
    }
    
    void run() {
        if (config.service_mode) {
            logMessage("Starting service mode...");
        } else {
            logMessage("Starting TimeWallpaper...");
            logMessage("Wallpaper will update every " + std::to_string(config.update_interval_minutes) + " minute(s)");
        }
        
        // Initial setup
        // Do location detection for service mode here (after service is marked as running)
        if (config.service_mode && config.auto_detect_location) {
            logMessage("Auto-detecting location...");
            autoDetectLocation();
        }
        
        fetchSolarTimes();
        generateTodaysColors();
        
        int updateCount = 0;
        std::string lastDate = getCurrentDate();
        
        while (true) {
            // Check if service is being stopped
            if (config.service_mode && g_ServiceStopEvent != INVALID_HANDLE_VALUE) {
                if (WaitForSingleObject(g_ServiceStopEvent, 0) == WAIT_OBJECT_0) {
                    logMessage("Service stop event received, exiting...");
                    break;
                }
            }
            
            try {
                // Check if we need to refresh solar times (new day)
                std::string currentDate = getCurrentDate();
                if (currentDate != lastDate) {
                    logMessage("New day detected, refreshing solar times...");
                    fetchSolarTimes();
                    generateTodaysColors();
                    lastDate = currentDate;
                }
                
                if (updateWallpaper()) {
                    updateCount++;
                    
                    if (config.debug_mode || updateCount % 1 == 0) { // Show status updates
                        time_t now = time(0);
                        tm* timeinfo = localtime(&now);
                        Color currentColor = getCurrentColor();
                        std::string period = getCurrentPeriod();
                        
                        std::string statusMsg = "[" + std::to_string(updateCount) + "] " 
                                               + formatHour(timeinfo->tm_hour + (timeinfo->tm_min / 60.0))
                                               + " | " + period 
                                               + " | Wallpaper RGB(" + std::to_string(currentColor.r) + ", " 
                                               + std::to_string(currentColor.g) + ", " 
                                               + std::to_string(currentColor.b) + ")";
                        
                        logMessage(statusMsg);
                    }
                }
                
                // Sleep for the specified interval
                std::this_thread::sleep_for(std::chrono::minutes(config.update_interval_minutes));
                
            } catch (const std::exception& e) {
                std::string errorMsg = "Error in main loop: " + std::string(e.what());
                logMessage(errorMsg);
                std::this_thread::sleep_for(std::chrono::minutes(1));
            }
        }
    }

    void logMessage(const std::string& message) {
        // Get the directory where the executable is located
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        std::string exeDir = std::string(exePath);
        size_t lastSlash = exeDir.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            exeDir = exeDir.substr(0, lastSlash + 1);
        } else {
            exeDir = "";
        }
        
        // Go up one directory level
        size_t parentSlash = exeDir.find_last_of("\\/", exeDir.length() - 2);
        std::string parentDir;
        if (parentSlash != std::string::npos) {
            parentDir = exeDir.substr(0, parentSlash + 1);
        } else {
            parentDir = exeDir; // Fallback to current directory if parent not found
        }
        
        std::string logPath = parentDir + "log.txt";
        std::ofstream logFile(logPath, std::ios::app);
        if (logFile.is_open()) {
            time_t now = time(0);
            tm* timeinfo = localtime(&now);
            
            char timestamp[100];
            strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);
            
            logFile << "[" << timestamp << "] " << message << std::endl;
            logFile.close();
        }
    }

    void setServiceMode(bool enabled) {
        config.service_mode = enabled;
    }
};

// Global app instance for service
TimeWallpaper* g_AppInstance = nullptr;

VOID WINAPI ServiceMain(DWORD argc, LPTSTR* argv);
VOID WINAPI ServiceCtrlHandler(DWORD);
DWORD WINAPI ServiceWorkerThread(LPVOID lpParam);

VOID WINAPI ServiceMain(DWORD argc, LPTSTR* argv) {
    DWORD Status = E_FAIL;

    g_StatusHandle = RegisterServiceCtrlHandlerA("TimeWallpaper", ServiceCtrlHandler);

    if (g_StatusHandle == NULL) {
        return;
    }

    ZeroMemory(&g_ServiceStatus, sizeof(g_ServiceStatus));
    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwControlsAccepted = 0;
    g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwServiceSpecificExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 0;

    if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE) {
        return;
    }

    g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (g_ServiceStopEvent == NULL) {
        g_ServiceStatus.dwControlsAccepted = 0;
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        g_ServiceStatus.dwWin32ExitCode = GetLastError();
        g_ServiceStatus.dwCheckPoint = 1;

        if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE) {
            return;
        }
        return;
    }

    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 0;

    if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE) {
        return;
    }

    HANDLE hThread = CreateThread(NULL, 0, ServiceWorkerThread, NULL, 0, NULL);

    WaitForSingleObject(hThread, INFINITE);

    CloseHandle(g_ServiceStopEvent);

    g_ServiceStatus.dwControlsAccepted = 0;
    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 3;

    if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE) {
        return;
    }
}

VOID WINAPI ServiceCtrlHandler(DWORD CtrlCode) {
    switch (CtrlCode) {
    case SERVICE_CONTROL_STOP:

        if (g_ServiceStatus.dwCurrentState != SERVICE_RUNNING)
            break;

        g_ServiceStatus.dwControlsAccepted = 0;
        g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
        g_ServiceStatus.dwWin32ExitCode = 0;
        g_ServiceStatus.dwCheckPoint = 4;

        if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE) {
            return;
        }

        SetEvent(g_ServiceStopEvent);
        break;

    default:
        break;
    }
}

DWORD WINAPI ServiceWorkerThread(LPVOID lpParam) {
    if (g_AppInstance) {
        try {
            g_AppInstance->run();
        } catch (...) {
            // Log any exceptions and set service to stopped
            g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
            g_ServiceStatus.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
            g_ServiceStatus.dwServiceSpecificExitCode = 1;
            SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        }
    }
    return ERROR_SUCCESS;
}

int main(int argc, char* argv[]) {
    if (argc > 1) {
        std::string mode = argv[1];
        if (mode == "--service" || mode == "-s") {
            // Running as Windows Service
            SERVICE_TABLE_ENTRY ServiceTable[] = {
                {(LPSTR)"TimeWallpaper", (LPSERVICE_MAIN_FUNCTION)ServiceMain},
                {NULL, NULL}
            };

            // Create app instance with service mode enabled  
            TimeWallpaper app;
            g_AppInstance = &app;
            g_AppInstance->setServiceMode(true);

            if (StartServiceCtrlDispatcher(ServiceTable) == FALSE) {
                return GetLastError();
            }
            return 0;
        }
    }

    // Regular application mode
    TimeWallpaper app;
    
    if (argc > 1) {
        std::string mode = argv[1];
        if (mode == "--help" || mode == "-h") {
            std::cout << "\nUsage:" << std::endl;
            std::cout << "  TimeWallpaper.exe              - Updates wallpaper colors throughout the day (runs silently)" << std::endl;
            std::cout << "  TimeWallpaper.exe --service    - Run as Windows Service (used by service manager)" << std::endl;
            std::cout << "  TimeWallpaper.exe --help       - Show this help" << std::endl;
            std::cout << "\nAll output is logged to log.txt file." << std::endl;
            std::cout << "Edit config.ini to set your location coordinates." << std::endl;
            std::cout << "\nTo install as Windows Service, run install_service.bat as administrator." << std::endl;
            return 0;
        }
    }
    
    app.run();
    return 0;
}