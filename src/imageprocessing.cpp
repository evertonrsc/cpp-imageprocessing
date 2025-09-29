/**
 * @file	imageprocessing.cpp
 * @brief	An image processing program that applies grayscale
 *          transformation to batches of images
 * @author	Everton Cavalcante (everton.cavalcante@ufrn.br)
 * @since	September 24, 2025
 * @date	September 28, 2025
 */

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

/** @brief Generative AI model */
#define GENAI_MODEL "gemini-2.5-flash-lite"

/** @brief Directory to store downloaded images */
# define IMAGES_DIR "images/"

/** @brief Directory to store processed images */
# define GSIMAGES_DIR "gs-images/"

/** @brief API key file for Google Gemini */
# define APIKEY_FILE "googleai.key"

/**
 * @brief A callback for libcurl.
 * @details When libcurl is used to to perform an HTTP request, it needs to
 *          know how to handle the incoming data (the response body).
 *          This function is registered with CURLOPT_WRITEFUNCTION, and each
 *          time libcurl receives a block of data, it calls this function.
 *
 * @param contents Pointer to the block of data received from the HTTP response
 * @param size The size (in bytes) of each data element
 * @param num_data The number of data elements
 * @param userp Pointer to a string that will hold the response
 * @return Number of bytes handled (size * num_data)
 */
size_t writeCallback(void* contents, size_t size, size_t num_data, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * num_data);
    return size * num_data;
}

/**
 * @brief Ensure that a directory exists, otherwise it creates the directory
 * 
 * @param dir Directory name
 */
void makeDir(const std::string& dir) {
    struct stat st;
    if (stat(dir.c_str(), &st) == -1) {
        mkdir(dir.c_str(), 0755);
    }
}

/**
 * @brief Check if a URL is accessible by making an HTTP request to it
 *
 * @param url URL to check
 * @return true if the request is successful, false otherwise
 */
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

/**
 * @brief Downloads an image from its URL
 *
 * @param url URL to the image
 * @param filename Name of the file for the downloaded image
 */
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

/**
 * @brief Applies grayscale transformation to an image using facilities from
 * OpenCV
 *
 * @param input_file Image file to process
 * @param output_file Resulting processed image file
 */
void toGrayscale(const std::string& input_file, const std::string& output_file) {
    cv::Mat image = cv::imread(input_file);
    if (image.empty()) {
        std::cerr << "Error: unable to read " << input_file << " file" << std::endl;
        return;
    }
    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    cv::imwrite(output_file, gray);
}

/**
 * @brief Make an HTTP POST request to the Google Gemini API
 * 
 * @param apiKey API key to interact with the API
 * @param prompt Prompt to be executed on Google Gemini
 * @return Output provided by Google Gemini 
 */
std::string postToGemini(const std::string& apiKey, const std::string& prompt) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string readBuffer;
    std::string url =
        "https://generativelanguage.googleapis.com/v1beta/models/" +
        std::string(GENAI_MODEL) + ":generateContent?key=" +
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
        std::cerr << "Error in request: " << curl_easy_strerror(res) << std::endl;
        return "";
    }

    return readBuffer;
}

/**
 * @brief Extract text from Google Gemini response
 * 
 * @param response Response in JSON format
 * @return Extracted text
 */
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
/**
 * @brief Generates a list of public domain image URL from public domain image
 *        repositories on the Web
 * @details Google Gemini is utilized to generate the list of image URLs in a
 *          two-step process with two prompts. The first one generates URLs
 *          directly pointing to a valid image file in either JPEG or PNG format. The
 *          second one is used to extract only the list of URLs from the output of the
 *          first prompt as there is no guarantee that the first prompt generates only the
 *          list of image URLs
 *
 * @param apiKey API key to Google Gemini
 * @param numimages Number of images to generate
 * @return List of image URLs
 */
std::vector<std::string> generateImageUrls(const std::string& apiKey, int numimages) {
    std::vector<std::string> image_urls;
    while (image_urls.size() < (size_t)numimages) {
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

/**
 * @brief Main function
 * 
 * @param argc Number of command-line arguments
 * @param argv Command-line arguments
 * @return Execution status
 */
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

    if (argc == 2) {
        int numimages = atoi(argv[1]);
        std::vector<std::string> imageUrls = generateImageUrls(apiKey, numimages);

        // For each image URL, downloads the image and converts to grayscale
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