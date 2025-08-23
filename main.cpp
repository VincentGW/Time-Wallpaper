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
        points.push_back({0.0, Color(8, 8, 25), "Deep Night"});
        points.push_back({std::max(1.0, sunrise - 3.0), Color(15, 10, 35), "Pre-Dawn"});
        points.push_back({std::max(2.0, sunrise - 1.5), Color(25, 15, 45), "Early Dawn"});
        points.push_back({std::max(3.0, sunrise - 1.0), Color(45, 25, 65), "Early Dawn"});
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
        points.push_back({std::max(13.0, solar_noon + 1.0), Color(170, 210, 235), "Early Afternoon"});
        points.push_back({std::max(14.0, solar_noon + 1.5), Color(170, 210, 230), "Early Afternoon"});
        
        // Afternoon to sunset - ensure progressive timing
        double late_afternoon_start = std::max(15.0, sunset - 2.0);
        double pre_sunset_start = std::max(late_afternoon_start + 0.5, sunset - 1.0);
        double pre_sunset_mid = std::max(pre_sunset_start + 0.25, sunset - 0.5);
        double pre_sunset_end = std::max(pre_sunset_mid + 0.25, sunset - 0.25);
        double sunset_time = std::max(pre_sunset_end + 0.25, sunset);
        
        points.push_back({late_afternoon_start, Color(170, 200, 230), "Late Afternoon"});
        points.push_back({pre_sunset_start, Color(170, 190, 230), "Late Afternoon"});
        points.push_back({pre_sunset_mid, Color(170, 170, 200), "Pre-Sunset"});
        points.push_back({pre_sunset_end, Color(180, 160, 160), "Pre-Sunset"});
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
        points.push_back({twilight_3, Color(60, 55, 75), "Civil Twilight"});
        
        // Evening progression - based on twilight end
        double evening_start = twilight_3 + 0.25;
        points.push_back({evening_start, Color(50, 50, 70), "Evening"});
        points.push_back({evening_start + 0.25, Color(50, 45, 70), "Evening"});
        points.push_back({evening_start + 0.5, Color(50, 40, 60), "Evening"});
        points.push_back({evening_start + 0.75, Color(50, 35, 60), "Evening"});
        points.push_back({evening_start + 1.0, Color(45, 30, 55), "Evening"});
        points.push_back({evening_start + 1.25, Color(45, 30, 45), "Evening"});
        points.push_back({evening_start + 1.5, Color(35, 30, 40), "Evening"});
        points.push_back({evening_start + 1.75, Color(35, 28, 40), "Late Evening"});
        points.push_back({evening_start + 2.25, Color(30, 22, 40), "Late Evening"});
        points.push_back({evening_start + 2.75, Color(20, 17, 40), "Late Evening"});
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
        points.push_back({std::max(1.0, sunrise - 3.0), Color(15, 10, 35), "Pre-Dawn"});
        points.push_back({std::max(2.0, sunrise - 1.5), Color(25, 15, 45), "Early Dawn"});
        points.push_back({std::max(3.0, sunrise - 1.0), Color(45, 25, 65), "Early Dawn"});
        points.push_back({std::max(4.0, sunrise - 0.5), Color(80, 50, 90), "Dawn"});
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
        points.push_back({std::max(13.0, solar_noon + 1.0), Color(180, 210, 220), "Early Afternoon"});
        points.push_back({std::max(14.0, solar_noon + 1.5), Color(190, 200, 210), "Early Afternoon"});
        
        // Afternoon to sunset - ensure progressive timing
        double late_afternoon_start = std::max(15.0, sunset - 2.0);
        double pre_sunset_start = std::max(late_afternoon_start + 0.5, sunset - 1.0);
        double pre_sunset_mid = std::max(pre_sunset_start + 0.25, sunset - 0.5);
        double pre_sunset_end = std::max(pre_sunset_mid + 0.25, sunset - 0.25);
        double sunset_time = std::max(pre_sunset_end + 0.25, sunset);
        
        points.push_back({late_afternoon_start, Color(200, 180, 150), "Late Afternoon"});
        points.push_back({pre_sunset_start, Color(210, 170, 130), "Late Afternoon"});
        points.push_back({pre_sunset_mid, Color(220, 160, 110), "Pre-Sunset"});
        points.push_back({pre_sunset_end, Color(230, 150, 90), "Pre-Sunset"});
        points.push_back({sunset_time, Color(230, 140, 70), "Sunset"});
        
        // Post-sunset to evening - ensure monotonic progression
        double post_sunset_1 = sunset_time + 0.25;
        double post_sunset_2 = post_sunset_1 + 0.25;
        double post_sunset_3 = post_sunset_2 + 0.25;
        double twilight_1 = post_sunset_3 + 0.25;
        double twilight_2 = twilight_1 + 0.25;
        double twilight_3 = std::max(twilight_2 + 0.25, civil_twilight_end);
        
        points.push_back({post_sunset_1, Color(210, 120, 60), "Sunset"});
        points.push_back({post_sunset_2, Color(170, 100, 70), "Post-Sunset"});
        points.push_back({post_sunset_3, Color(140, 90, 75), "Post-Sunset"});
        points.push_back({twilight_1, Color(110, 80, 80), "Civil Twilight"});
        points.push_back({twilight_2, Color(95, 75, 80), "Civil Twilight"});
        points.push_back({twilight_3, Color(80, 65, 75), "Civil Twilight"});
        
        // Evening progression - based on twilight end
        double evening_start = twilight_3 + 0.25;
        points.push_back({evening_start, Color(70, 60, 75), "Evening"});
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