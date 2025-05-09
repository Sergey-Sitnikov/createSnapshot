#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <ctime>
#include <fstream>
#include <curl/curl.h>
// #include <jsoncpp/json/json.h>
#include <filesystem>
#include <vector>

std::vector<std::pair<double, double>> urls = {{38.989986, 45.044545}, {39.007159, 45.122231}};

// struct SnapshotConfig
// {
//     std::string area_coordinates;
//     std::string save_path;
//     std::string file_name_format;
int capture_interval = 3600;
std::string start_time = "07:00";
std::string end_time = "23:00";
// };

void createSnapshot(std::pair<double, double> url, std::string addres, std::string format, int i, tm *current_time)
// void createSnapshot(const SnapshotConfig &config)
{

    // Проверка времени
    // if (current_time->tm_hour < std::stoi(config.start_time.substr(0, 2)) ||
    //     current_time->tm_hour >= std::stoi(config.end_time.substr(0, 2)))
    // {
    //     return; // Не в пределах времени
    // }

    // Форматирование имени файла
    char buffer[80];
    std::strftime(buffer, sizeof(buffer), format.c_str(), current_time);
    std::string file_name = addres + "/" + buffer + '_' + std::to_string(i) + ".png"; // Добавляем индекс к имени файла

    // Проверка, существует ли папка
    if (!std::filesystem::exists(addres))
    {
        std::cerr << "Каталог для сохранения не существует: " << addres << std::endl;
        return;
    }

    // Запрос к API
    // std::string api_url = "https://api.yandex.com/maps/v1/?ll=" + config.area_coordinates + "&size=650,450&l=map";
    // curl "https://api.yandex.com/maps/v1/?ll=-39.48425,36.87645&size=650,450&l=map"

    // std::string api_url = "https://yandex.ru/maps/35/krasnodar/probki/?ll=38.981268%2C45.042113&z=15.31";
    // std::string api_url = "https://static-maps.yandex.ru/1.x/?ll=38.969717,45.032623&size=650,450&z=15&l=map,trf";
    std::string api_url = "https://static-maps.yandex.ru/1.x/?ll=" +
                          std::to_string(url.first) + "," + std::to_string(url.second) +
                          "&size=650,450&z=13&l=map,trf";

    CURL *curl = curl_easy_init();
    if (curl)
    {
        FILE *file = fopen(file_name.c_str(), "wb");
        if (file)
        {
            curl_easy_setopt(curl, CURLOPT_URL, api_url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, file); // Установка параметра для записи данных
            curl_easy_setopt(curl, CURLOPT_HEADER, 0L);      // Не получать заголовки в файл
            curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L); // Остановить при HTTP-ошибках
            curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);     // Включаем вывод отладки

            CURLcode res = curl_easy_perform(curl);
            fclose(file);

            if (res != CURLE_OK)
            {
                std::cerr << "Ошибка при выполнении запроса: " << curl_easy_strerror(res) << std::endl;
                std::remove(file_name.c_str()); // Удалите файл, если возникла ошибка
            }
            else
            {
                // Проверка размера файла
                std::ifstream ifs(file_name, std::ios::binary | std::ios::ate);
                std::streamsize size = ifs.tellg();
                ifs.close();

                if (size == 0)
                {
                    std::cerr << "Ошибка: файл " << file_name << " пустой." << std::endl;
                    std::remove(file_name.c_str()); // Удалите пустой файл
                }
                else
                {
                    std::cout << "Скриншот сохранен: " << file_name << std::endl;
                }
            }
        }
        else
        {
            std::cerr << "Ошибка при открытии файла: " << file_name << std::endl;
        }
        curl_easy_cleanup(curl);
    }
}

// void startSnapshotting(const SnapshotConfig &config)
// {
//     while (true)
//     {
//         createSnapshot(config);
//         std::this_thread::sleep_for(std::chrono::seconds(config.capture_interval));
//     }
// }

int main()
{
    // SnapshotConfig config = {
    //     //"-39.48425,36.87645",
    //     "-45.044659,38.964670", // Убедитесь, что координаты правильные
    //     "./screenshots",
    //     "Скрин %Y-%m-%d в %H-%M",
    //     3600,
    //     "07:00",
    //     "23:00"};

    // Создание папки для сохранения, если она не существует
    // if (!std::filesystem::exists(config.save_path))
    // {
    //     std::filesystem::create_directory(config.save_path);
    // }
    while (true)
    {
        std::time_t now = std::time(nullptr);
        std::tm *current_time = std::localtime(&now);
        if (current_time->tm_hour < std::stoi(start_time.substr(0, 2)) ||
            current_time->tm_hour >= std::stoi(end_time.substr(0, 2)))
        {
            continue; // Не в пределах времени
        }
        for (int u = urls.size() - 1; u >= 0; u--)
        {
            std::string name = "Скрин %Y-%m-%d в %H-%M" + ' ' + u;
            createSnapshot(urls[u], "./screenshots", "Скрин %Y-%m-%d в %H-%M", u, current_time);
        }
        std::this_thread::sleep_for(std::chrono::seconds(capture_interval));
    }

    return 0;
}