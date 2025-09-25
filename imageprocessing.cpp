#include <curl/curl.h>
#include <sys/stat.h>

#include <fstream>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <sstream>
#include <string>
#include <vector>

// Include the single-header JSON library (json.hpp downloaded locally)
#include "nlohmann/json.hpp"

using json = nlohmann::json;

// cURL write callback
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// Ensure directory exists (like Python os.makedirs)
void makeDir(const std::string& dir) {
    struct stat st = {0};
    if (stat(dir.c_str(), &st) == -1) {
        mkdir(dir.c_str(), 0755);
    }
}

bool isAccessible(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    CURLcode res;
    long response_code = 0;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    }
    curl_easy_cleanup(curl);

    return (res == CURLE_OK && response_code == 200);
}

bool downloadImage(const std::string& url, const std::string& filename) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    FILE* fp = fopen(filename.c_str(), "wb");
    if (!fp) return false;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, nullptr);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    fclose(fp);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK);
}

void toGrayscale(const std::string& input, const std::string& output) {
    cv::Mat image = cv::imread(input);
    if (image.empty()) {
        std::cerr << "Error: could not read " << input << std::endl;
        return;
    }
    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    cv::imwrite(output, gray);
}

// POST to Gemini API
std::string postToGemini(const std::string& apiKey, const std::string& prompt) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string readBuffer;
    std::string url =
        "https://generativelanguage.googleapis.com/v1beta/models/"
        "gemini-2.5-flash-lite:generateContent?key=" +
        apiKey;

    // Gemini request JSON
    json body = {
        {"contents", {{{"role", "user"}, {"parts", {{{"text", prompt}}}}}}}};
    std::string jsonData = body.dump();

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonData.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "Error in request: " << curl_easy_strerror(res)
                  << std::endl;
        return "";
    }

    return readBuffer;
}

// Extract text from Gemini JSON response
std::string extractTextFromGemini(const std::string& response) {
    try {
        json j = json::parse(response);
        if (j.contains("candidates") && !j["candidates"].empty()) {
            return j["candidates"][0]["content"]["parts"][0]["text"];
        }
    } catch (std::exception& e) {
        std::cerr << "Error parsing Gemini response: " << e.what() << std::endl;
    }
    return "";
}

// Generate image URLs (two-step: generate -> extract)
std::vector<std::string> generateImageUrls(const std::string& apiKey,
                                           int numImages) {
    std::vector<std::string> urls;

    // Step 1: Generate raw text with URLs
    std::ostringstream generationPrompt;
    generationPrompt << "Generate " << numImages
                     << " public domain image URL (JPEG or PNG, no Wikimedia). "
                     << "The URL must point directly to a valid image file "
                        "ending with .jpg or .png, "
                     << "and be less than 200 KB. Provide plain text output.";

    std::string generationResponse =
        postToGemini(apiKey, generationPrompt.str());
    std::string genText = extractTextFromGemini(generationResponse);

    if (genText.empty()) {
        std::cerr << "Gemini generation returned empty text." << std::endl;
        return urls;
    }

    // Step 2: Extract only URLs
    std::ostringstream extractionPrompt;
    extractionPrompt << "Extract all URLs from the following text. "
                     << "Return them as a plain text list, one URL per line.\n"
                     << "Text: " << genText;

    std::string extractionResponse =
        postToGemini(apiKey, extractionPrompt.str());
    std::string urlsText = extractTextFromGemini(extractionResponse);

    if (urlsText.empty()) {
        std::cerr << "Gemini extraction returned empty text." << std::endl;
        return urls;
    }

    // Split lines and validate URLs
    std::istringstream iss(urlsText);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.find("http") != std::string::npos) {
            urls.push_back(line);
        }
    }

    std::vector<std::string> validUrls;
    for (const auto& url : urls) {
        if (isAccessible(url)) {
            validUrls.push_back(url);
            if (validUrls.size() == (size_t)numImages) break;
        }
    }

    return validUrls;
}

int main() {
    std::ifstream keyFile("googleai.key");
    if (!keyFile) {
        std::cerr << "Missing googleai.key" << std::endl;
        return 1;
    }
    std::string apiKey;
    std::getline(keyFile, apiKey);

    // Create images folder
    makeDir("images");
    makeDir("gs-images");

    // Generate URLs from Gemini
    std::vector<std::string> imageUrls = generateImageUrls(apiKey, 2);

    for (size_t i = 0; i < imageUrls.size(); i++) {
        std::string filename = "images/" + std::to_string(i + 1) + ".jpg";
        if (downloadImage(imageUrls[i], filename)) {
            std::cout << "Downloaded: " << filename << std::endl;
            std::string grayFile =
                "gs-images/" + std::to_string(i + 1) + ".jpg";
            toGrayscale(filename, grayFile);
            std::cout << "Saved grayscale: " << grayFile << std::endl;
        } else {
            std::cerr << "Failed to download: " << imageUrls[i] << std::endl;
        }
    }

    return 0;
}