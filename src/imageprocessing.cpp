#include <curl/curl.h>
#include <sys/stat.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <sstream>
#include <string>
#include <vector>

// Include the single-header JSON library (json.hpp downloaded locally)
#include "json.hpp"
using json = nlohmann::json;

# define IMAGES_DIR "images/"
# define GSIMAGES_DIR "gs-images/"
# define APIKEY_FILE "googleai.key"

// curl write callback
size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// Ensure directory exists
void makeDir(const std::string& dir) {
    struct stat st;
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

void downloadImage(const std::string& url, const std::string& filename) {
    CURL* curl = curl_easy_init();
    if (!curl) exit(1);

    FILE* fp = fopen(filename.c_str(), "wb");
    if (!fp) exit(1);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, nullptr);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    fclose(fp);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) return;
}

void toGrayscale(const std::string& input, const std::string& output) {
    cv::Mat image = cv::imread(input);
    if (image.empty()) {
        std::cerr << "Error: unable to read " << input << std::endl;
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

    // Google Gemini body request in JSON
    json body = {
        {"contents", {{{"role", "user"}, {"parts", {{{"text", prompt}}}}}}}};
    std::string jsonData = body.dump();

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonData.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
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

// Extract text from Google Gemini JSON response
std::string extractTextFromGemini(const std::string& response) {
    try {
        json j = json::parse(response);
        if (j.contains("candidates") && !j["candidates"].empty()) {
            return j["candidates"][0]["content"]["parts"][0]["text"];
        }
    } catch (std::exception& e) {
        std::cerr << "Error when parsing response from Google Gemini: " << 
            e.what() << std::endl;
    }
    return "";
}

// Generate image URLs (two-step: generate -> extract)
std::vector<std::string> generateImageUrls(const std::string& apiKey, int numimages) {
    std::vector<std::string> image_urls;
    while (image_urls.size() < (size_t)numimages) {
        // Step 1: Generate image URLs
        std::ostringstream generationPrompt;
        generationPrompt << "Generate " << numimages
                         << " public domain image URLs (either JPEG or PNG format)" 
                         << " from trusted public domain image repositories. Exclude"
                         << " Wikimedia Commons and related sites. The URL must directly"
                         << " point to a valid image file ending with.jpg or .png, and"
                         << " the file size must be less than 200 KB. Provide the final"
                         << " image URLs in plain text.";
        std::string generationResponse =
            postToGemini(apiKey, generationPrompt.str());
        std::string genText = extractTextFromGemini(generationResponse);

        // Step 2: Extract only URLs from the output of the previous prompt
        std::ostringstream extractionPrompt;
        extractionPrompt
            << "Extract all URLs from the following contents into a plain text "
               "list. Each URL must be on a new line. These are the contents: "
            << genText;
        std::string extractionResponse =
            postToGemini(apiKey, extractionPrompt.str());
        std::string urlsText = extractTextFromGemini(extractionResponse);

        // Split lines and validate URLs
        std::vector<std::string> candidate_urls;
        std::istringstream iss(urlsText);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.find("http") != std::string::npos) {
                candidate_urls.push_back(line);
            }
        }

        // Check if URLs are accessible
        for (const auto& url : candidate_urls) {
            if (isAccessible(url)) {
                image_urls.push_back(url);
                if (image_urls.size() == (size_t)numimages) break;
            }
        }
    }
    
    return image_urls;
}

int main(int argc, char* argv[]) {
    std::ifstream keyFile(APIKEY_FILE);
    if (!keyFile) {
        std::cerr << "API key file is missing" << std::endl;
        return 1;
    }
    std::string apiKey;
    std::getline(keyFile, apiKey);

    // Create images directories
    makeDir(IMAGES_DIR);
    makeDir(GSIMAGES_DIR);

    // Generate URLs from Google Gemini
    if (argc == 2) {
        int numimages = atoi(argv[1]);
        std::vector<std::string> imageUrls = generateImageUrls(apiKey, numimages);

        for (size_t i = 0; i < imageUrls.size(); i++) {
            std::string filename = IMAGES_DIR + std::to_string(i + 1) + ".jpg";
            std::string grayFile = GSIMAGES_DIR + std::to_string(i + 1) + ".jpg";
            downloadImage(imageUrls[i], filename);
            toGrayscale(filename, grayFile);
        }

        return 0;
    } else {
        std::cerr << "Error: the number of images to process is missing." << std::endl;
        return 1;
    }
}