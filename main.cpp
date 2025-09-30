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
#include <gdiplus.h>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "gdiplus.lib")

// Power management for wake-from-sleep detection
HWND g_hWnd = NULL;
bool g_justWokeUp = false;

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
    std::string source; // "api", "cache", or "fallback"
};

struct SolarCache {
    std::vector<SolarTimes> days; // 8 days: today + next 7 days
    std::string last_updated;
};

struct Config {
    double latitude = 40.7128;   // Default: NYC
    double longitude = -74.0060;
    std::string location_name = "New York City";
    int update_interval_minutes = 1;
    bool debug_mode = false;
    bool auto_detect_location = true;
};

class TimeWallpaper {
private:
    int screenWidth, screenHeight;
    std::string tempPath;
    Config config;
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    
    struct ColorPoint {
        double hour;
        Color color;
        std::string period;
    };
    
    std::vector<ColorPoint> todaysColors;
    SolarTimes todaysSolarTimes;
    SolarCache solarCache;
    std::string lastFetchDate;

public:
    TimeWallpaper() {
        Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
        
        screenWidth = GetSystemMetrics(SM_CXSCREEN);
        screenHeight = GetSystemMetrics(SM_CYSCREEN);
        
        char tempDir[MAX_PATH];
        GetTempPathA(MAX_PATH, tempDir);
        tempPath = std::string(tempDir) + "time_wallpaper.bmp";
        
        loadConfig();
        
        if (config.auto_detect_location) {
            autoDetectLocation();
        }
        
        // Load existing solar cache
        loadSolarCache();

        logMessage("TimeWallpaper v2.0 - Solar Edition (8-Day Cache)");
        logMessage("=================================================");
        logMessage("Location: " + config.location_name + " (" + std::to_string(config.latitude) + ", " + std::to_string(config.longitude) + ")");
        logMessage("Update interval: " + std::to_string(config.update_interval_minutes) + " minute(s)");
        logMessage("Screen: " + std::to_string(screenWidth) + "x" + std::to_string(screenHeight));
        logMessage("Solar cache: " + getSolarCachePath() + " (8-day storage)");
    }
    
    ~TimeWallpaper() {
        Gdiplus::GdiplusShutdown(gdiplusToken);
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
            
            // Get current system timezone info to determine if DST is active
            TIME_ZONE_INFORMATION tzi;
            DWORD tziResult = GetTimeZoneInformation(&tzi);
            
            double offsetHours;
            if (tziResult == TIME_ZONE_ID_DAYLIGHT) {
                // DST is active - use daylight bias
                offsetHours = -(tzi.Bias + tzi.DaylightBias) / 60.0;
            } else {
                // Standard time - use standard bias
                offsetHours = -(tzi.Bias + tzi.StandardBias) / 60.0;
            }
            
            double localHour = utcHour + offsetHours;
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
    
    bool fetchSolarTimesForDate(const std::string& targetDate, SolarTimes& solarTimes, bool isRetry = false) {
        std::stringstream urlBuilder;
        urlBuilder << "https://api.sunrise-sunset.org/json?lat=" << config.latitude
                   << "&lng=" << config.longitude << "&date=" << targetDate << "&formatted=0";

        std::string url = urlBuilder.str();
        if (config.debug_mode) logMessage("Fetching solar times for " + targetDate + "..." + std::string(isRetry ? " (retry)" : ""));

        std::string response = httpGetWithTimeout(url, isRetry ? 10000 : 5000);

        if (response.empty()) {
            if (!isRetry) {
                if (config.debug_mode) logMessage("API request timed out, retrying once...");
                return fetchSolarTimesForDate(targetDate, solarTimes, true);
            }
            if (config.debug_mode) logMessage("API Error: Request timed out for " + targetDate);
            return false;
        }

        if (response.find("\"status\":\"OK\"") == std::string::npos) {
            if (config.debug_mode) logMessage("API Error: Invalid response for " + targetDate);
            return false;
        }

        try {
            solarTimes.sunrise_hour = parseTimeFromJson(response, "\"sunrise\":");
            solarTimes.sunset_hour = parseTimeFromJson(response, "\"sunset\":");
            solarTimes.solar_noon_hour = parseTimeFromJson(response, "\"solar_noon\":");
            solarTimes.civil_twilight_begin = parseTimeFromJson(response, "\"civil_twilight_begin\":");
            solarTimes.civil_twilight_end = parseTimeFromJson(response, "\"civil_twilight_end\":");

            if (solarTimes.sunrise_hour > 0 && solarTimes.sunset_hour > 0) {
                solarTimes.valid = true;
                solarTimes.fetch_date = targetDate;
                solarTimes.source = "api";

                if (config.debug_mode) {
                    logMessage("Solar times fetched successfully for " + targetDate + ":");
                    logMessage("  Sunrise: " + formatHour(solarTimes.sunrise_hour));
                    logMessage("  Sunset: " + formatHour(solarTimes.sunset_hour));
                }

                return true;
            }
        } catch (...) {
            if (config.debug_mode) logMessage("JSON parsing error for " + targetDate);
        }

        return false;
    }

    bool fetchEightDaySolarData() {
        // Initialize cache with 8 days if not already sized
        if (solarCache.days.size() != 8) {
            solarCache.days.resize(8);
        }

        int successCount = 0;

        // Fetch data for today and next 7 days (8 days total)
        for (int dayOffset = 0; dayOffset < 8; dayOffset++) {
            std::string targetDate = getDateOffset(dayOffset);
            if (fetchSolarTimesForDate(targetDate, solarCache.days[dayOffset])) {
                successCount++;
            }
        }

        if (successCount > 0) {
            solarCache.last_updated = getCurrentDate();
            saveSolarCache();
            logMessage("Updated solar cache with " + std::to_string(successCount) + "/8 days from API");
            return true;
        }

        logMessage("Failed to fetch any solar data from API");
        return false;
    }

    SolarTimes* findCachedDataForDate(const std::string& targetDate) {
        for (size_t i = 0; i < solarCache.days.size(); i++) {
            if (solarCache.days[i].fetch_date == targetDate) {
                return &solarCache.days[i];
            }
        }
        return nullptr;
    }

    bool shouldUpdateCache() {
        std::string today = getCurrentDate();

        // Only update once per day - if we already updated today, don't update again
        if (solarCache.last_updated == today) {
            return false;
        }

        return true;
    }

    bool fetchSolarTimes(bool forceRefresh = false) {
        std::string today = getCurrentDate();

        // Load cache first
        loadSolarCache();

        // Check if we have valid cached data for today
        SolarTimes* todaysData = findCachedDataForDate(today);
        if (!forceRefresh && todaysData && todaysData->valid) {
            todaysSolarTimes = *todaysData;
            if (config.debug_mode) logMessage("Using cached solar times for " + today + " (source: " + todaysData->source + ")");
            return true;
        }

        // Only attempt API fetch if we haven't already updated today
        if (shouldUpdateCache()) {
            logMessage("Attempting daily solar data update for " + today + " (8 days)");
            if (fetchEightDaySolarData()) {
                // Use today's data if available
                todaysData = findCachedDataForDate(today);
                if (todaysData && todaysData->valid) {
                    todaysSolarTimes = *todaysData;
                    return true;
                }
            }
        } else {
            if (config.debug_mode) logMessage("Skipping API update - already updated today");
        }

        // Fall back to cached data if available (any cached data for today)
        todaysData = findCachedDataForDate(today);
        if (todaysData && todaysData->valid) {
            todaysSolarTimes = *todaysData;
            logMessage("Using cached solar times for " + today + " (source: " + todaysData->source + ")");
            return true;
        }

        // Check for any available cached data as backup (search all 8 days)
        for (size_t i = 0; i < solarCache.days.size(); i++) {
            if (solarCache.days[i].valid) {
                todaysSolarTimes = solarCache.days[i];
                todaysSolarTimes.fetch_date = today;
                todaysSolarTimes.source = "cache-backup-day" + std::to_string(i);
                logMessage("Using cached solar times from day " + std::to_string(i) + " as backup for " + today);
                return true;
            }
        }

        // Last resort: use January averages
        logMessage("No cached data available. Using fallback times.");
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
        todaysSolarTimes.source = "fallback";

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

    std::string getDateOffset(int dayOffset) {
        time_t now = time(0);
        now += dayOffset * 24 * 60 * 60; // Add/subtract days
        tm* timeinfo = localtime(&now);

        std::stringstream ss;
        ss << (timeinfo->tm_year + 1900) << "-"
           << std::setfill('0') << std::setw(2) << (timeinfo->tm_mon + 1) << "-"
           << std::setw(2) << timeinfo->tm_mday;
        return ss.str();
    }

    std::string getSolarCachePath() {
        std::string configPath = getConfigPath();
        size_t lastSlash = configPath.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            return configPath.substr(0, lastSlash + 1) + "solar_cache.txt";
        }
        return "solar_cache.txt";
    }

    void saveSolarCache() {
        std::string cachePath = getSolarCachePath();
        std::ofstream cacheFile(cachePath);
        if (!cacheFile.is_open()) {
            if (config.debug_mode) logMessage("Failed to save solar cache to " + cachePath);
            return;
        }

        cacheFile << "# TimeWallpaper Solar Cache - Eight Day Data (Today + Next 7 Days)" << std::endl;
        cacheFile << "last_updated=" << solarCache.last_updated << std::endl;
        cacheFile << std::endl;

        // Save all 8 days of data
        for (size_t i = 0; i < solarCache.days.size(); i++) {
            const SolarTimes& dayData = solarCache.days[i];

            cacheFile << "[day" << i << "]" << std::endl;
            cacheFile << "date=" << dayData.fetch_date << std::endl;
            cacheFile << "sunrise=" << std::fixed << std::setprecision(6) << dayData.sunrise_hour << std::endl;
            cacheFile << "sunset=" << std::fixed << std::setprecision(6) << dayData.sunset_hour << std::endl;
            cacheFile << "solar_noon=" << std::fixed << std::setprecision(6) << dayData.solar_noon_hour << std::endl;
            cacheFile << "civil_twilight_begin=" << std::fixed << std::setprecision(6) << dayData.civil_twilight_begin << std::endl;
            cacheFile << "civil_twilight_end=" << std::fixed << std::setprecision(6) << dayData.civil_twilight_end << std::endl;
            cacheFile << "valid=" << (dayData.valid ? "true" : "false") << std::endl;
            cacheFile << "source=" << dayData.source << std::endl;
            cacheFile << std::endl;
        }

        cacheFile.close();
        if (config.debug_mode) logMessage("Solar cache saved successfully (8 days)");
    }

    bool loadSolarCache() {
        std::string cachePath = getSolarCachePath();
        std::ifstream cacheFile(cachePath);
        if (!cacheFile.is_open()) {
            if (config.debug_mode) logMessage("No existing solar cache found");
            return false;
        }

        // Initialize with 8 empty entries
        solarCache.days.clear();
        solarCache.days.resize(8);

        std::string line;
        int currentDayIndex = -1;

        while (std::getline(cacheFile, line)) {
            if (line.empty() || line[0] == '#') continue;

            // Check for day sections [day0], [day1], etc.
            if (line.substr(0, 4) == "[day" && line.back() == ']') {
                std::string dayStr = line.substr(4, line.length() - 5);
                currentDayIndex = std::stoi(dayStr);
                if (currentDayIndex < 0 || currentDayIndex >= 8) {
                    currentDayIndex = -1; // Invalid index
                }
                continue;
            }

            size_t equalPos = line.find('=');
            if (equalPos != std::string::npos) {
                std::string key = line.substr(0, equalPos);
                std::string value = line.substr(equalPos + 1);

                if (key == "last_updated") {
                    solarCache.last_updated = value;
                } else if (currentDayIndex >= 0 && currentDayIndex < 8) {
                    SolarTimes& currentTimes = solarCache.days[currentDayIndex];
                    if (key == "date") currentTimes.fetch_date = value;
                    else if (key == "sunrise") currentTimes.sunrise_hour = std::stod(value);
                    else if (key == "sunset") currentTimes.sunset_hour = std::stod(value);
                    else if (key == "solar_noon") currentTimes.solar_noon_hour = std::stod(value);
                    else if (key == "civil_twilight_begin") currentTimes.civil_twilight_begin = std::stod(value);
                    else if (key == "civil_twilight_end") currentTimes.civil_twilight_end = std::stod(value);
                    else if (key == "valid") currentTimes.valid = (value == "true");
                    else if (key == "source") currentTimes.source = value;
                }
            }
        }

        cacheFile.close();
        if (config.debug_mode) logMessage("Solar cache loaded successfully (8 days)");
        return true;
    }
    
    void generateTodaysColors() {
        // This method is now simplified - we calculate colors on demand
        // rather than pre-generating discrete points
        if (config.debug_mode) {
            logMessage("Solar-based continuous color calculation initialized");
            logMessage("  Sunrise: " + formatHour(todaysSolarTimes.sunrise_hour));
            logMessage("  Solar Noon: " + formatHour(todaysSolarTimes.solar_noon_hour));
            logMessage("  Sunset: " + formatHour(todaysSolarTimes.sunset_hour));
            logMessage("  Civil Twilight End: " + formatHour(todaysSolarTimes.civil_twilight_end));
            
            // Generate CSV file with RGB values for each minute of the day
            generateDebugCSV();
        }
    }
    
    void generateDebugCSV() {
        std::string csvPath = getConfigPath();
        size_t lastSlash = csvPath.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            csvPath = csvPath.substr(0, lastSlash + 1) + "daily_colors.csv";
        } else {
            csvPath = "daily_colors.csv";
        }
        
        std::ofstream csvFile(csvPath);
        if (!csvFile.is_open()) {
            logMessage("Failed to create debug CSV file: " + csvPath);
            return;
        }
        
        // Write CSV header
        csvFile << "Time,Hour,Minute,R,G,B,Period" << std::endl;
        
        // Generate colors for each minute of the day
        for (int hour = 0; hour < 24; hour++) {
            for (int minute = 0; minute < 60; minute++) {
                double timeHour = hour + (minute / 60.0);
                Color color = getColorForHour(timeHour);
                std::string period = getPeriodForHour(timeHour);
                
                // Format time as HH:MM
                std::stringstream timeStr;
                timeStr << std::setfill('0') << std::setw(2) << hour 
                       << ":" << std::setw(2) << minute;
                
                csvFile << timeStr.str() << "," 
                       << std::fixed << std::setprecision(2) << timeHour << ","
                       << minute << ","
                       << color.r << "," << color.g << "," << color.b << ","
                       << period << std::endl;
            }
        }
        
        csvFile.close();
        logMessage("Debug CSV generated: " + csvPath);
    }
    
    std::string getPeriodForHour(double hour) {
        double sunrise = todaysSolarTimes.sunrise_hour;
        double sunset = todaysSolarTimes.sunset_hour;
        double solar_noon = todaysSolarTimes.solar_noon_hour;
        double civil_twilight_end = todaysSolarTimes.civil_twilight_end;
        
        // Create the same monotonic timeline for period detection
        std::vector<ColorPoint> points;
        
        // Night and early morning
        points.push_back({0.0, Color(6, 6, 8), "Deep Night"});
        points.push_back({std::max(1.0, sunrise - 3.0), Color(10, 10, 25), "Pre-Dawn"});
        points.push_back({std::max(2.0, sunrise - 1.5), Color(15, 15, 35), "Early Dawn"});
        points.push_back({std::max(3.0, sunrise - 1.0), Color(35, 15, 45), "Early Dawn"});
        points.push_back({std::max(4.0, sunrise - 0.5), Color(80, 50, 90), "Dawn"});
        points.push_back({std::max(5.0, sunrise - 0.25), Color(120, 80, 110), "Dawn"});
        
        // Sunrise and morning
        points.push_back({std::max(6.0, sunrise), Color(160, 120, 130), "Sunrise"});
        points.push_back({std::max(7.0, sunrise + 0.25), Color(190, 150, 140), "Sunrise"});
        points.push_back({std::max(8.0, sunrise + 0.5), Color(200, 200, 160), "Early Morning"});
        points.push_back({std::max(9.0, sunrise + 1.0), Color(215, 220, 180), "Early Morning"});
        points.push_back({std::max(10.0, sunrise + 2.0), Color(230, 240, 220), "Morning"});
        
        // Day time
        points.push_back({std::max(11.0, solar_noon - 1.5), Color(210, 235, 200), "Late Morning"});
        points.push_back({std::max(11.5, solar_noon - 1.0), Color(190, 220, 215), "Late Morning"});
        points.push_back({std::max(12.0, solar_noon), Color(170, 210, 240), "Noon"});
        points.push_back({std::max(13.0, solar_noon + 1.0), Color(170, 210, 240), "Early Afternoon"});
        points.push_back({std::max(14.0, solar_noon + 1.5), Color(170, 210, 240), "Early Afternoon"});
        
        // Afternoon to sunset - ensure progressive timing
        double late_afternoon_start = std::max(15.0, sunset - 2.0);
        double pre_sunset_start = std::max(late_afternoon_start + 0.5, sunset - 1.0);
        double pre_sunset_mid = std::max(pre_sunset_start + 0.25, sunset - 0.5);
        double pre_sunset_end = std::max(pre_sunset_mid + 0.25, sunset - 0.25);
        double sunset_time = std::max(pre_sunset_end + 0.25, sunset);
        
        points.push_back({late_afternoon_start, Color(170, 210, 235), "Late Afternoon"});
        points.push_back({pre_sunset_start, Color(170, 200, 230), "Late Afternoon"});
        points.push_back({pre_sunset_mid, Color(170, 200, 230), "Pre-Sunset"});
        points.push_back({pre_sunset_end, Color(180, 200, 225), "Pre-Sunset"});
        points.push_back({sunset_time, Color(220, 145, 70), "Sunset"});
        
        // Post-sunset to evening - ensure monotonic progression
        double post_sunset_1 = sunset_time + 0.25;
        double post_sunset_2 = post_sunset_1 + 0.25;
        double post_sunset_3 = post_sunset_2 + 0.25;
        double twilight_1 = post_sunset_3 + 0.25;
        double twilight_2 = twilight_1 + 0.25;
        double twilight_3 = std::max(twilight_2 + 0.25, civil_twilight_end);
        
        points.push_back({post_sunset_1, Color(230, 140, 70), "Sunset"});
        points.push_back({post_sunset_2, Color(210, 120, 60), "Post-Sunset"});
        points.push_back({post_sunset_3, Color(140, 90, 75), "Post-Sunset"});
        points.push_back({twilight_1, Color(110, 70, 80), "Civil Twilight"});
        points.push_back({twilight_2, Color(75, 65, 80), "Civil Twilight"});
        points.push_back({twilight_3, Color(55, 45, 70), "Civil Twilight"});
        
        // Evening progression - based on twilight end
        double evening_start = twilight_3 + 0.25;
        points.push_back({evening_start, Color(45, 40, 60), "Evening"});
        points.push_back({evening_start + 0.25, Color(40, 35, 50), "Evening"});
        points.push_back({evening_start + 0.5, Color(35, 30, 45), "Evening"});
        points.push_back({evening_start + 0.75, Color(30, 25, 35), "Evening"});
        points.push_back({evening_start + 1.0, Color(25, 20, 35), "Evening"});
        points.push_back({evening_start + 1.25, Color(25, 20, 30), "Evening"});
        points.push_back({evening_start + 1.5, Color(22, 18, 25), "Evening"});
        points.push_back({evening_start + 1.75, Color(20, 15, 25), "Late Evening"});
        points.push_back({evening_start + 2.25, Color(15, 12, 20), "Late Evening"});
        points.push_back({evening_start + 2.75, Color(12, 10, 18), "Late Evening"});
        points.push_back({evening_start + 3.25, Color(10, 10, 14), "Night"});
        points.push_back({23.99, Color(8, 8, 12), "Night"});
        
        // Fix times outside 0-24 range
        for (auto& point : points) {
            while (point.hour < 0) point.hour += 24.0;
            while (point.hour >= 24) point.hour -= 24.0;
        }
        
        // Sort points by time
        std::sort(points.begin(), points.end(), 
                  [](const ColorPoint& a, const ColorPoint& b) {
                      return a.hour < b.hour;
                  });
        
        // Find which period this hour falls into
        for (size_t i = 0; i < points.size() - 1; i++) {
            if (hour >= points[i].hour && hour <= points[i + 1].hour) {
                return points[i].period;
            }
        }
        
        // Handle wrap-around
        if (hour >= points.back().hour) {
            return points.back().period;
        }
        
        return points.front().period;
    }
    
    Color getCurrentColor() {
        time_t now = time(0);
        tm* timeinfo = localtime(&now);
        double currentHour = timeinfo->tm_hour + (timeinfo->tm_min / 60.0);
        
        return getColorForHour(currentHour);
    }
    
    
    Color getColorForHour(double hour) {
        double sunrise = todaysSolarTimes.sunrise_hour;
        double sunset = todaysSolarTimes.sunset_hour;
        double solar_noon = todaysSolarTimes.solar_noon_hour;
        double civil_twilight_end = todaysSolarTimes.civil_twilight_end;
        
        // Normalize hour to 0-24 range
        while (hour < 0) hour += 24.0;
        while (hour >= 24) hour -= 24.0;
        
        // Create base timeline points ensuring no overlaps
        std::vector<ColorPoint> points;
        
        // Night and early morning
        points.push_back({0.0, Color(8, 8, 25), "Deep Night"});
        points.push_back({std::max(1.0, sunrise - 3.0), Color(10, 10, 25), "Pre-Dawn"});
        points.push_back({std::max(2.0, sunrise - 1.5), Color(15, 15, 45), "Early Dawn"});
        points.push_back({std::max(3.0, sunrise - 1.0), Color(25, 15, 65), "Early Dawn"});
        points.push_back({std::max(4.0, sunrise - 0.5), Color(50, 30, 65), "Dawn"});
        points.push_back({std::max(5.0, sunrise - 0.25), Color(120, 80, 110), "Dawn"});
        
        // Sunrise and morning
        points.push_back({std::max(6.0, sunrise), Color(160, 120, 130), "Sunrise"});
        points.push_back({std::max(7.0, sunrise + 0.25), Color(190, 150, 140), "Sunrise"});
        points.push_back({std::max(8.0, sunrise + 0.5), Color(210, 180, 160), "Early Morning"});
        points.push_back({std::max(9.0, sunrise + 1.0), Color(220, 200, 180), "Early Morning"});
        points.push_back({std::max(10.0, sunrise + 2.0), Color(230, 240, 220), "Morning"});
        
        // Day time
        points.push_back({std::max(11.0, solar_noon - 1.5), Color(210, 230, 200), "Late Morning"});
        points.push_back({std::max(11.5, solar_noon - 1.0), Color(190, 220, 190), "Late Morning"});
        points.push_back({std::max(12.0, solar_noon), Color(170, 210, 230), "Noon"});
        points.push_back({std::max(13.0, solar_noon + 1.0), Color(170, 210, 230), "Early Afternoon"});
        points.push_back({std::max(14.0, solar_noon + 1.5), Color(170, 210, 230), "Early Afternoon"});
        
        // Afternoon to sunset - ensure progressive timing
        double late_afternoon_start = sunset - 2.0;
        double pre_sunset_start = sunset - 1.5;
        double pre_sunset_mid = sunset - 1.0;
        double pre_sunset_end = sunset - 0.5;
        double sunset_time = sunset;
        
        points.push_back({late_afternoon_start, Color(170, 210, 230), "Late Afternoon"});
        points.push_back({pre_sunset_start, Color(170, 210, 230), "Late Afternoon"});
        points.push_back({pre_sunset_mid, Color(175, 200, 225), "Pre-Sunset"});
        points.push_back({pre_sunset_end, Color(180, 195, 220), "Pre-Sunset"});
        points.push_back({sunset_time, Color(230, 140, 70), "Sunset"});
        
        // Post-sunset to evening - ensure monotonic progression
        double post_sunset_1 = sunset_time + 0.1;
        double post_sunset_2 = post_sunset_1 + 0.1;
        double post_sunset_3 = post_sunset_2 + 0.1;
        double twilight_1 = post_sunset_3 + 0.1;
        double twilight_2 = twilight_1 + 0.1;
        double twilight_3 = twilight_2 + 0.1;
        
        points.push_back({post_sunset_1, Color(210, 120, 70), "Sunset"});
        points.push_back({post_sunset_2, Color(170, 100, 75), "Post-Sunset"});
        points.push_back({post_sunset_3, Color(140, 90, 80), "Post-Sunset"});
        points.push_back({twilight_1, Color(110, 80, 85), "Civil Twilight"});
        points.push_back({twilight_2, Color(95, 75, 95), "Civil Twilight"});
        points.push_back({twilight_3, Color(80, 65, 85), "Civil Twilight"});
        
        // Evening progression - based on twilight end
        double evening_start = twilight_3 + 0.1;
        points.push_back({evening_start, Color(65, 60, 75), "Evening"});
        points.push_back({evening_start + 0.25, Color(65, 55, 70), "Evening"});
        points.push_back({evening_start + 0.5, Color(60, 50, 70), "Evening"});
        points.push_back({evening_start + 0.75, Color(55, 45, 65), "Evening"});
        points.push_back({evening_start + 1.0, Color(50, 40, 65), "Evening"});
        points.push_back({evening_start + 1.25, Color(45, 35, 60), "Evening"});
        points.push_back({evening_start + 1.5, Color(40, 30, 55), "Evening"});
        points.push_back({evening_start + 1.75, Color(38, 28, 52), "Late Evening"});
        points.push_back({evening_start + 2.25, Color(30, 22, 48), "Late Evening"});
        points.push_back({evening_start + 2.75, Color(22, 17, 42), "Late Evening"});
        points.push_back({evening_start + 3.25, Color(15, 12, 35), "Night"});
        points.push_back({23.99, Color(8, 8, 25), "Night"});
        
        // Fix times outside 0-24 range
        for (auto& point : points) {
            while (point.hour < 0) point.hour += 24.0;
            while (point.hour >= 24) point.hour -= 24.0;
        }
        
        // Sort points by time
        std::sort(points.begin(), points.end(), 
                  [](const ColorPoint& a, const ColorPoint& b) {
                      return a.hour < b.hour;
                  });
        
        // Find the appropriate color interpolation
        for (size_t i = 0; i < points.size() - 1; i++) {
            if (hour >= points[i].hour && hour <= points[i + 1].hour) {
                double progress = (hour - points[i].hour) / (points[i + 1].hour - points[i].hour);
                return interpolateColor(points[i].color, points[i + 1].color, progress);
            }
        }
        
        // Handle wrap-around (from last point to first point)
        double lastHour = points.back().hour;
        double firstHour = points.front().hour + 24;
        
        if (hour >= lastHour) {
            double progress = (hour - lastHour) / (firstHour - lastHour);
            return interpolateColor(points.back().color, points.front().color, progress);
        }
        
        return points.front().color;
    }
    
    bool isTimeBetween(double current, double start, double end) {
        // Handle case where time range wraps around midnight
        if (start > end) {
            return (current >= start || current <= end);
        }
        return (current >= start && current <= end);
    }
    
    double getProgressBetween(double current, double start, double end) {
        if (start > end) {
            // Handle wrap around midnight
            if (current >= start) {
                return (current - start) / (24.0 - start + end);
            } else {
                return (24.0 - start + current) / (24.0 - start + end);
            }
        }
        return (current - start) / (end - start);
    }
    
    std::string getCurrentPeriod() {
        time_t now = time(0);
        tm* timeinfo = localtime(&now);
        double currentHour = timeinfo->tm_hour + (timeinfo->tm_min / 60.0);
        
        return getPeriodForHour(currentHour);
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
        return createWatermarkedWallpaper(color);
    }
    
    bool createWatermarkedWallpaper(Color bgColor) {
        HDC hdcScreen = GetDC(NULL);
        HDC hdcMemory = CreateCompatibleDC(hdcScreen);
        HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, screenWidth, screenHeight);
        SelectObject(hdcMemory, hBitmap);
        
        HBRUSH hBrush = CreateSolidBrush(RGB(bgColor.r, bgColor.g, bgColor.b));
        RECT fillRect = {0, 0, screenWidth, screenHeight};
        FillRect(hdcMemory, &fillRect, hBrush);
        DeleteObject(hBrush);
        
        Gdiplus::Graphics graphics(hdcMemory);
        
        std::string configPath = getConfigPath();
        size_t lastSlash = configPath.find_last_of("\\/");
        std::string watermarkPath;
        if (lastSlash != std::string::npos) {
            watermarkPath = configPath.substr(0, lastSlash + 1) + "Watermark.png";
        } else {
            watermarkPath = "Watermark.png";
        }
        std::wstring watermarkPathW(watermarkPath.begin(), watermarkPath.end());
        
        Gdiplus::Image watermarkImage(watermarkPathW.c_str());
        if (watermarkImage.GetLastStatus() == Gdiplus::Ok) {
            int watermarkWidth = watermarkImage.GetWidth();
            int watermarkHeight = watermarkImage.GetHeight();
            
            int x = (screenWidth - watermarkWidth) / 2;
            int y = (screenHeight - watermarkHeight) / 2 - 20;
            
            Gdiplus::ColorMatrix colorMatrix = {
                1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 0.1f, 0.0f,
                0.0f, 0.0f, 0.0f, 0.0f, 1.0f
            };
            
            Gdiplus::ImageAttributes imageAttributes;
            imageAttributes.SetColorMatrix(&colorMatrix, Gdiplus::ColorMatrixFlagsDefault, Gdiplus::ColorAdjustTypeBitmap);
            
            graphics.DrawImage(&watermarkImage, 
                              Gdiplus::Rect(x, y, watermarkWidth, watermarkHeight),
                              0, 0, watermarkWidth, watermarkHeight,
                              Gdiplus::UnitPixel, &imageAttributes);
        }
        
        if (!setWallpaperFromBitmap(hBitmap)) {
            DeleteObject(hBitmap);
            DeleteDC(hdcMemory);
            ReleaseDC(NULL, hdcScreen);
            return false;
        }
        
        DeleteObject(hBitmap);
        DeleteDC(hdcMemory);
        ReleaseDC(NULL, hdcScreen);
        
        return true;
    }
    
    bool setWallpaperFromBitmap(HBITMAP hBitmap) {
        static std::wstring previousWallpaperPath;
        
        wchar_t tempPath[MAX_PATH];
        GetTempPathW(MAX_PATH, tempPath);
        
        SYSTEMTIME st;
        GetSystemTime(&st);
        wchar_t timeStr[64];
        swprintf_s(timeStr, L"watermarked_%04d%02d%02d_%02d%02d%02d_%03d.bmp", 
                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        std::wstring currentWallpaperPath = std::wstring(tempPath) + timeStr;
        
        CLSID bmpClsid;
        GetEncoderClsid(L"image/bmp", &bmpClsid);
        
        Gdiplus::Bitmap bitmap(hBitmap, NULL);
        if (bitmap.Save(currentWallpaperPath.c_str(), &bmpClsid, NULL) != Gdiplus::Ok) {
            return false;
        }
        
        bool result = SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (LPVOID)currentWallpaperPath.c_str(), 0);
        
        if (result) {
            Sleep(3000);
            
            if (!previousWallpaperPath.empty()) {
                DeleteFileW(previousWallpaperPath.c_str());
            }
            cleanupOldWallpapers(currentWallpaperPath);
            
            previousWallpaperPath = currentWallpaperPath;
        }
        
        return result;
    }
    
    void cleanupOldWallpapers(const std::wstring& currentFile) {
        wchar_t tempPath[MAX_PATH];
        GetTempPathW(MAX_PATH, tempPath);
        std::wstring searchPath = std::wstring(tempPath) + L"watermarked_*.bmp";
        
        WIN32_FIND_DATAW findData;
        HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
        
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                std::wstring fullPath = std::wstring(tempPath) + findData.cFileName;
                if (fullPath != currentFile) {
                    DeleteFileW(fullPath.c_str());
                }
            } while (FindNextFileW(hFind, &findData));
            FindClose(hFind);
        }
    }
    
    int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
        UINT num = 0;
        UINT size = 0;
        
        Gdiplus::ImageCodecInfo* pImageCodecInfo = NULL;
        
        Gdiplus::GetImageEncodersSize(&num, &size);
        if (size == 0) return -1;
        
        pImageCodecInfo = (Gdiplus::ImageCodecInfo*)(malloc(size));
        if (pImageCodecInfo == NULL) return -1;
        
        Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);
        
        for (UINT j = 0; j < num; ++j) {
            if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
                *pClsid = pImageCodecInfo[j].Clsid;
                free(pImageCodecInfo);
                return j;
            }
        }
        
        free(pImageCodecInfo);
        return -1;
    }
    
    
    
    
    bool updateWallpaper() {
        Color currentColor = getCurrentColor();

        if (!createSolidColorBitmap(currentColor)) {
            logMessage("Failed to create watermarked wallpaper");
            return false;
        }

        return true;
    }
    
    void createMessageWindow() {
        // Create a hidden window to receive power management messages
        WNDCLASSA wc = {0};
        wc.lpfnWndProc = PowerEventWndProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = "TimeWallpaperPowerWindow";
        RegisterClassA(&wc);
        
        g_hWnd = CreateWindowA("TimeWallpaperPowerWindow", "TimeWallpaper Power", 
                              0, 0, 0, 0, 0, HWND_MESSAGE, NULL, GetModuleHandle(NULL), this);
    }

    static LRESULT CALLBACK PowerEventWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        if (uMsg == WM_POWERBROADCAST) {
            if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) {
                g_justWokeUp = true;
                // Get the TimeWallpaper instance from window data
                TimeWallpaper* instance = (TimeWallpaper*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
                if (instance) {
                    instance->logMessage("System resumed from sleep - updating wallpaper immediately");
                }
            }
        }
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }

    void run() {
        logMessage("Starting TimeWallpaper...");
        logMessage("Wallpaper will update every " + std::to_string(config.update_interval_minutes) + " minute(s)");
        
        // Create hidden window for power management messages
        createMessageWindow();
        if (g_hWnd) {
            SetWindowLongPtr(g_hWnd, GWLP_USERDATA, (LONG_PTR)this);
            logMessage("Power management enabled - will update on wake from sleep");
        }
        
        // Initial setup
        
        fetchSolarTimes();
        generateTodaysColors();
        
        int updateCount = 0;
        std::string lastDate = getCurrentDate();
        
        while (true) {
            // Process Windows messages for power events
            MSG msg;
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            
            try {
                // Check if we just woke up from sleep
                bool forceUpdate = false;
                if (g_justWokeUp) {
                    logMessage("Wake from sleep detected - checking cached data first");

                    // Try to use cached data before forcing API refresh
                    std::string today = getCurrentDate();
                    loadSolarCache();

                    SolarTimes* todaysData = findCachedDataForDate(today);
                    if (todaysData && todaysData->valid) {
                        todaysSolarTimes = *todaysData;
                        logMessage("Using cached solar data after wake (source: " + todaysData->source + ")");
                    } else {
                        // Use any available cached data as backup from the 8-day cache
                        bool foundBackup = false;
                        for (size_t i = 0; i < solarCache.days.size(); i++) {
                            if (solarCache.days[i].valid) {
                                todaysSolarTimes = solarCache.days[i];
                                todaysSolarTimes.fetch_date = today;
                                todaysSolarTimes.source = "cache-wake-day" + std::to_string(i);
                                logMessage("Using cached data from day " + std::to_string(i) + " after wake as backup");
                                foundBackup = true;
                                break;
                            }
                        }

                        if (!foundBackup) {
                            // No cached data available, try to fetch fresh data
                            logMessage("No cached data available after wake - attempting fresh fetch");
                            fetchSolarTimes(true); // Force refresh
                        }
                    }

                    forceUpdate = true;
                    g_justWokeUp = false;
                }
                
                // Check if we need to refresh solar times (new day)
                std::string currentDate = getCurrentDate();
                if (currentDate != lastDate) {
                    logMessage("New day detected, refreshing solar times...");
                    fetchSolarTimes();
                    generateTodaysColors();
                    lastDate = currentDate;
                    forceUpdate = true;
                }

                
                if (updateWallpaper() || forceUpdate) {
                    updateCount++;
                    
                    if (config.debug_mode || updateCount % 1 == 0) { // Show status updates
                        time_t now = time(0);
                        tm* timeinfo = localtime(&now);
                        Color currentColor = getCurrentColor();
                        std::string period = getCurrentPeriod();
                        
                        std::string statusMsg = "[" + std::to_string(updateCount) + "] "
                                               + formatHour(timeinfo->tm_hour + (timeinfo->tm_min / 60.0))
                                               + " | " + period
                                               + " | RGB(" + std::to_string(currentColor.r) + ", "
                                               + std::to_string(currentColor.g) + ", "
                                               + std::to_string(currentColor.b) + ")"
                                               + " | Source: " + todaysSolarTimes.source;
                        
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

};

int main(int argc, char* argv[]) {
    TimeWallpaper app;
    
    if (argc > 1) {
        std::string mode = argv[1];
        if (mode == "--help" || mode == "-h") {
            std::cout << "\nUsage:" << std::endl;
            std::cout << "  TimeWallpaper.exe              - Updates wallpaper colors throughout the day" << std::endl;
            std::cout << "  TimeWallpaper.exe --help       - Show this help" << std::endl;
            std::cout << "\nFeatures:" << std::endl;
            std::cout << "  • Automatic location detection via IP geolocation" << std::endl;
            std::cout << "  • Real astronomical data for your location" << std::endl;
            std::cout << "  • Wake from sleep detection - updates immediately on resume" << std::endl;
            std::cout << "  • All output is logged to log.txt file" << std::endl;
            std::cout << "\nEdit config.ini to set manual location coordinates if needed." << std::endl;
            return 0;
        }
    }
    
    app.run();
    return 0;
}