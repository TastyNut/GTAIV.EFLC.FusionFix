module;

#include <filesystem>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <algorithm>
#include "zhl.h" // Assuming FusionFix uses a logging system like zhl.h

export module AddonLoader;

export class AddonLoader {
public:
    AddonLoader() {
        LoadAddons();
    }

    void LoadAddons() {
        std::string episode = GetCurrentEpisode();
        std::filesystem::path updatePath = "update";

        if (!std::filesystem::exists(updatePath)) {
            ZHL::Log("No /update/ directory found.\n");
            return;
        }

        // Collect all add-on files
        for (const auto& modDir : std::filesystem::directory_iterator(updatePath)) {
            if (modDir.is_directory()) {
                std::string modName = modDir.path().filename().string();
                std::filesystem::path episodePath = modDir.path() / episode;

                if (std::filesystem::exists(episodePath)) {
                    for (const auto& file : std::filesystem::directory_iterator(episodePath)) {
                        std::string filename = file.path().filename().string();
                        if (IsAddonFile(filename)) {
                            std::string originalFile = GetOriginalFileName(filename, modName);
                            if (!originalFile.empty()) {
                                addonFiles[originalFile].push_back(file.path().string());
                            }
                        }
                    }
                }
            }
        }

        // Process each original file and its add-ons
        for (const auto& pair : addonFiles) {
            const std::string& originalFile = pair.first;
            const std::vector<std::string>& addons = pair.second;
            MergeAddons(originalFile, addons);
        }
    }

private:
    std::map<std::string, std::vector<std::string>> addonFiles;

    std::string GetCurrentEpisode() {
        // Placeholder: Determine episode based on game state (IV, TBOGT, TLAD)
        // This would need to hook into FusionFix's episode detection
        return "IV";
    }

    bool IsAddonFile(const std::string& filename) {
        return filename.find(".addon.") != std::string::npos &&
               (filename.ends_with(".ide") || filename.ends_with(".dat") ||
                filename.ends_with(".txt") || filename.ends_with(".xml") ||
                filename.ends_with(".wpl"));
    }

    std::string GetOriginalFileName(const std::string& addonFile, const std::string& modName) {
        size_t addonPos = addonFile.find("." + modName + ".addon.");
        if (addonPos != std::string::npos) {
            std::string baseName = addonFile.substr(0, addonPos);
            std::string ext = addonFile.substr(addonPos + modName.length() + 7); // +7 for ".addon."
            return baseName + ext;
        }
        ZHL::Log("Invalid add-on filename: %s\n", addonFile.c_str());
        return "";
    }

    std::string LoadFile(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            ZHL::Log("Failed to open file: %s\n", path.c_str());
            return "";
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    void MergeAddons(const std::string& originalFile, const std::vector<std::string>& addons) {
        // Load original file (assume it’s in /common/data/ or similar)
        std::string originalPath = "common/data/" + originalFile; // Adjust path as needed
        std::string originalData = LoadFile(originalPath);
        if (originalData.empty()) {
            ZHL::Log("Original file not found or empty: %s\n", originalFile.c_str());
            return;
        }

        std::vector<std::string> sections;
        ParseSections(originalData, sections);

        // Process each add-on file
        for (const std::string& addon : addons) {
            std::string addonData = LoadFile(addon);
            if (!addonData.empty()) {
                ApplyAddonCommands(sections, addonData);
            }
        }

        std::string mergedData = ReconstructData(sections);
        UpdateGameData(originalFile, mergedData);
    }

    void ParseSections(const std::string& data, std::vector<std::string>& sections) {
        std::stringstream ss(data);
        std::string line, currentSection;
        while (std::getline(ss, line)) {
            line.erase(0, line.find_first_not_of(" \t")); // Trim leading whitespace
            if (line.empty() || line[0] == '#') continue;

            if (line == "peds" || line == "agrps" || line == "end") {
                if (!currentSection.empty()) sections.push_back(currentSection);
                currentSection = line + "\n";
            } else if (!currentSection.empty()) {
                currentSection += line + "\n";
            }
        }
        if (!currentSection.empty()) sections.push_back(currentSection);
    }

    void ApplyAddonCommands(std::vector<std::string>& sections, const std::string& addonData) {
        std::stringstream ss(addonData);
        std::string line, currentSection;
        std::string replaceTarget;

        while (std::getline(ss, line)) {
            line.erase(0, line.find_first_not_of(" \t"));
            if (line.empty() || line[0] == '#') continue;

            if (line == "peds" || line == "agrps" || line == "end") {
                currentSection = line;
                continue;
            }

            if (line.substr(0, 7) == "\"Append") {
                std::string entry = line.substr(8) + "\n";
                for (auto& section : sections) {
                    if (section.substr(0, currentSection.length()) == currentSection) {
                        section.insert(section.length() - 5, entry); // Before "end\n"
                        break;
                    }
                }
            } else if (line.substr(0, 7) == "\"Remove") {
                std::string entry = line.substr(8) + "\n";
                for (auto& section : sections) {
                    if (section.substr(0, currentSection.length()) == currentSection) {
                        size_t pos = section.find(entry);
                        if (pos != std::string::npos) {
                            section.erase(pos, entry.length());
                        }
                        break;
                    }
                }
            } else if (line.substr(0, 8) == "\"Replace") {
                replaceTarget = line.substr(9) + "\n";
            } else if (line.substr(0, 5) == "\"with") {
                std::string newEntry = line.substr(6) + "\n";
                for (auto& section : sections) {
                    if (section.substr(0, currentSection.length()) == currentSection) {
                        size_t pos = section.find(replaceTarget);
                        if (pos != std::string::npos) {
                            section.replace(pos, replaceTarget.length(), newEntry);
                        }
                        break;
                    }
                }
                replaceTarget.clear();
            }
        }
    }

    std::string ReconstructData(const std::vector<std::string>& sections) {
        std::string result;
        for (const auto& section : sections) {
            result += section;
        }
        return result;
    }

    void UpdateGameData(const std::string& originalFile, const std::string& mergedData) {
        // Placeholder: Hook into game’s file loading or memory
        // This would require specific FusionFix hooks or reverse-engineered addresses
        ZHL::Log("Merged data for %s ready. Integration pending game hook.\n", originalFile.c_str());
        // Example: Inject mergedData into game memory or override file read
    }
};

// Initialization (integrate with FusionFix’s startup)
static AddonLoader addonLoader;