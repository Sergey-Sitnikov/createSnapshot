#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QTimeEdit>
#include <QPushButton>
#include <QMessageBox>
#include <QFileDialog>
#include <QListWidget>
#include <QThread>
#include <QIntValidator>
#include <QDoubleValidator>
#include <QGroupBox>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <curl/curl.h>
#include <ctime>
#include <fstream>
#include <chrono>
#include <thread>
#include <cmath>
#include <limits>
#include <cstdio>
#include <algorithm>
#include <vector>
#include <cerrno>
#include <locale>
#include <sstream> // Для std::ostringstream

// Qt Image/Painter/Dir includes
#include <QImage>
#include <QPainter>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QFile>
#include <QtGlobal>

// Класс для хранения параметров одного объекта карты
class MapObject
{
public:
    double latitude_center;
    double longitude_center;
    int radius_km;
    std::string name;           // Уникальное имя объекта для файлов и логов
    std::string save_directory; // Каталог для сохранения снимков этого объекта

    MapObject(double lat, double lon, int rad_km, std::string obj_name, std::string save_dir)
        : latitude_center(lat), longitude_center(lon), radius_km(rad_km), name(std::move(obj_name)), save_directory(std::move(save_dir)) {}

    // Для отображения в QListWidget
    QString getDisplayText() const
    {
        return QString("Имя: %1, Центр: (%2, %3), Радиус: %4 км, Путь: %5")
            .arg(QString::fromStdString(name))
            .arg(latitude_center)
            .arg(longitude_center)
            .arg(radius_km)
            .arg(QString::fromStdString(save_directory));
    }
};

// Название временной папки (базовое)
const std::string screen_temp_directory_name_base = "screen_temp";

// Вспомогательная функция для объединения изображений из временного каталога в один BMP файл.
// Она обрабатывает файлы из temp_dir и сохраняет результат в output_dir.
// Горизонтальные ряды объединяются в обратном порядке.
// Возвращает true в случае успеха, false в случае неудачи.
bool combineAndCleanupScreenshots(const std::string &temp_dir_path,
                                  const std::string &output_dir_path, // Это базовый путь для объекта
                                  std::tm *current_time,
                                  const std::string &object_name_identifier)
{
    QDir tempDir(QString::fromStdString(temp_dir_path));
    if (!tempDir.exists())
    {
        std::cerr << "Временный каталог для объединения не существует: " << temp_dir_path << std::endl;
        std::error_code ec;
        std::filesystem::remove_all(temp_dir_path, ec); // Попытка очистки
        if (ec)
        {
            std::cerr << "Ошибка при очистке несуществующего временного каталога (filesystem): " << ec.message() << std::endl;
        }
        return false;
    }

    QStringList filters;
    filters << "*.png";
    QFileInfoList fileList = tempDir.entryInfoList(filters, QDir::Files, QDir::Name);

    if (fileList.isEmpty())
    {
        std::cerr << "Временные скриншоты в каталоге " << temp_dir_path << " не найдены. Объединение пропущено для объекта " << object_name_identifier << "." << std::endl;
        std::error_code ec;
        std::filesystem::remove_all(temp_dir_path, ec);
        if (ec)
        {
            std::cerr << "Ошибка при очистке пустого временного каталога (filesystem): " << ec.message() << std::endl;
        }
        return false;
    }

    std::sort(fileList.begin(), fileList.end(), [](const QFileInfo &a, const QFileInfo &b)
              {
        QString name_a = a.baseName();
        QString name_b = b.baseName();
        int last_underscore_a = name_a.lastIndexOf('_');
        int last_underscore_b = name_b.lastIndexOf('_');
        int index_a = (last_underscore_a != -1) ? name_a.mid(last_underscore_a + 1).toInt() : -1;
        int index_b = (last_underscore_b != -1) ? name_b.mid(last_underscore_b + 1).toInt() : -1;
        return index_a < index_b; });

    int total_images = fileList.size();
    int N_grid_dim = static_cast<int>(std::sqrt(static_cast<double>(total_images)));

    if (N_grid_dim * N_grid_dim != total_images)
    {
        std::cerr << "Ошибка для объекта " << object_name_identifier << ": Общее количество скриншотов (" << total_images
                  << ") не является полным квадратом. Невозможно сформировать сетку " << N_grid_dim << "x" << N_grid_dim
                  << ". Временные файлы сохранены в " << temp_dir_path << std::endl;
        return false; // НЕ очищать временные файлы в этом случае
    }

    QImage firstImage(fileList.first().absoluteFilePath());
    if (firstImage.isNull())
    {
        std::cerr << "Ошибка загрузки первого изображения для объекта " << object_name_identifier << ": "
                  << fileList.first().absoluteFilePath().toStdString() << std::endl;
        std::error_code ec;
        std::filesystem::remove_all(temp_dir_path, ec);
        if (ec)
        {
            std::cerr << "Ошибка при очистке filesystem после сбоя загрузки первого изображения: " << ec.message() << std::endl;
        }
        return false;
    }

    int img_width = firstImage.width();
    int img_height = firstImage.height();
    int composite_width = N_grid_dim * img_width;
    int composite_height = N_grid_dim * img_height;

    QImage compositeImage(composite_width, composite_height, QImage::Format_RGB32);
    compositeImage.fill(Qt::white);
    QPainter painter(&compositeImage);

    for (int i = 0; i < total_images; ++i)
    {
        QImage img(fileList.at(i).absoluteFilePath());
        if (img.isNull())
        {
            std::cerr << "Ошибка загрузки изображения: " << fileList.at(i).absoluteFilePath().toStdString() << ". Пропуск." << std::endl;
            continue;
        }
        int row = i / N_grid_dim;
        int col = i % N_grid_dim;
        int composite_row = (N_grid_dim - 1) - row;
        painter.drawImage(col * img_width, composite_row * img_height, img);
    }
    painter.end();

    char time_str_buffer[80];
    std::strftime(time_str_buffer, sizeof(time_str_buffer), "%Y-%m-%d_%H-%M-%S", current_time);

    // Путь сохранения теперь берется из объекта, переданного потоку
    // Уникальное имя файла включает имя объекта
    std::string safe_object_name = object_name_identifier;
    std::replace_if(safe_object_name.begin(), safe_object_name.end(), [](char c)
                    { return !std::isalnum(c) && c != '_' && c != '-'; }, '_');

    std::string output_file_name = output_dir_path + "/" + safe_object_name + "_" + time_str_buffer + ".bmp";

    // Убедиться, что выходной каталог объекта существует перед сохранением
    std::error_code ec_dir;
    std::filesystem::create_directories(output_dir_path, ec_dir);
    if (ec_dir)
    {
        std::cerr << "Ошибка создания выходного каталога для объекта " << object_name_identifier << ": " << output_dir_path << " - " << ec_dir.message() << std::endl;
        // Продолжить очистку временной папки, даже если не удалось сохранить
    }

    bool save_success = false;
    if (!ec_dir)
    { // Только если удалось создать выходной каталог
        save_success = compositeImage.save(QString::fromStdString(output_file_name), "BMP");
    }

    std::error_code ec_cleanup;
    std::filesystem::remove_all(temp_dir_path, ec_cleanup);
    if (ec_cleanup)
    {
        std::cerr << "Ошибка при очистке временного каталога '" << temp_dir_path << "' (filesystem): " << ec_cleanup.message() << std::endl;
    }
    else
    {
        std::cout << "Временный каталог '" << temp_dir_path << "' очищен." << std::endl;
    }

    if (save_success)
    {
        std::cout << "Композитный скриншот для объекта " << object_name_identifier << " сохранен: " << output_file_name << std::endl;
        return true;
    }
    else
    {
        std::cerr << "Ошибка сохранения композитного скриншота для объекта " << object_name_identifier << ": " << output_file_name << std::endl;
        return false;
    }
}

// Поток для выполнения захвата скриншотов
class CaptureThread : public QThread
{
    Q_OBJECT

public:
    CaptureThread(std::vector<MapObject> objects,
                  int interval,
                  std::string st_time,
                  std::string en_time,
                  QObject *parent = nullptr)
        : QThread(parent),
          m_mapObjects(std::move(objects)),
          m_capture_interval_sec(interval),
          m_start_time_str(std::move(st_time)),
          m_end_time_str(std::move(en_time)),
          running(false) {}

    void stop()
    {
        running = false;
    }

    void run() override
    {
        running = true;
        std::cout << "Поток захвата запущен." << std::endl;

        while (running)
        {
            std::time_t now_t = std::time(nullptr);
            std::tm *current_time_tm = std::localtime(&now_t);

            if (!current_time_tm)
            {
                std::cerr << "Ошибка получения текущего времени. Пропускаем цикл захвата." << std::endl;
                sleepAndCheckRunning(10); // Подождать 10 секунд
                continue;
            }

            // Проверка времени захвата
            if (!isWithinCaptureTimeWindow(current_time_tm))
            {
                // std::cout << "Вне времени захвата (" << m_start_time_str << " - " << m_end_time_str << "). Ожидаем..." << std::endl; // Слишком много логов
                sleepAndCheckRunning(300); // Ждать 5 минут перед следующей проверкой
                continue;
            }

            std::cout << "Внутри времени захвата. Запуск обработки объектов (" << m_mapObjects.size() << " шт.)." << std::endl;

            for (size_t i = 0; i < m_mapObjects.size() && running; ++i)
            {
                const auto &mapObject = m_mapObjects[i];
                std::cout << "Обработка объекта: " << mapObject.name << std::endl;

                std::vector<std::pair<double, double>> current_object_coords;
                generateCoordinatesForObject(mapObject, current_object_coords);

                if (current_object_coords.empty())
                {
                    std::cerr << "Нет координат для объекта " << mapObject.name << ". Пропуск." << std::endl;
                    continue;
                }

                // Временная папка теперь создается внутри каталога сохранения объекта
                std::string object_temp_dir_name = screen_temp_directory_name_base + "_" + mapObject.name;
                std::string object_temp_full_path = mapObject.save_directory + "/" + object_temp_dir_name;

                std::error_code ec;
                std::filesystem::remove_all(object_temp_full_path, ec); // Очистить перед новым пакетом
                if (ec)
                {
                    std::cerr << "Ошибка при очистке временного каталога объекта " << mapObject.name << ": " << ec.message() << std::endl;
                }
                // Убедиться, что и каталог сохранения объекта, и временный каталог существуют
                std::filesystem::create_directories(object_temp_full_path, ec);
                if (ec)
                {
                    std::cerr << "Ошибка создания временного каталога для объекта " << mapObject.name << ": " << object_temp_full_path << " - " << ec.message() << std::endl;
                    continue; // Пропустить этот объект
                }

                bool capture_loop_completed_for_object = true;
                for (size_t u = 0; u < current_object_coords.size() && running; ++u)
                {
                    std::time_t snap_time_t = std::time(nullptr); // Обновить время для каждого снимка
                    std::tm *snap_time_tm = std::localtime(&snap_time_t);
                    createSnapshot(current_object_coords[u], object_temp_full_path, "Скриншот_%Y-%m-%d_%H-%M-%S", static_cast<int>(u), snap_time_tm);
                }
                if (!running)
                { // Проверка после внутреннего цикла захвата
                    capture_loop_completed_for_object = false;
                    std::cout << "Запрошена остановка во время захвата для объекта " << mapObject.name << std::endl;
                }

                if (capture_loop_completed_for_object)
                {
                    // Обновить current_time_tm для имени файла объединенного изображения
                    now_t = std::time(nullptr);
                    current_time_tm = std::localtime(&now_t);
                    // Передаем каталог сохранения объекта как output_dir_path
                    combineAndCleanupScreenshots(object_temp_full_path, mapObject.save_directory, current_time_tm, mapObject.name);
                }
                else
                {
                    std::cerr << "Захват для объекта " << mapObject.name << " прерван. Очистка временных файлов." << std::endl;
                    std::filesystem::remove_all(object_temp_full_path, ec); // Очистить частичные результаты
                    if (ec)
                    {
                        std::cerr << "Ошибка при очистке частичного временного каталога объекта " << mapObject.name << ": " << ec.message() << std::endl;
                    }
                }
                if (!running)
                    break; // Выйти из цикла по объектам, если остановлено
            } // Конец цикла по объектам

            if (running)
            {
                // Получить текущий момент времени (высокой точности)
                auto now = std::chrono::system_clock::now();
                // Преобразовать в time_t (для совместимости со старыми функциями)
                std::time_t now_c = std::chrono::system_clock::to_time_t(now);
                // Преобразовать в локальное время (структура tm)
                std::tm *local_tm = std::localtime(&now_c);

                if (m_capture_interval_sec % 3600 != 0 || local_tm->tm_min == 0 || local_tm->tm_min == 1)
                { // Ждать следующий интервал, только если не остановлено
                    // std::cout << "Все объекты обработаны. Ожидание следующего интервала захвата (" << m_capture_interval_sec << "с)..." << std::endl;
                    sleepAndCheckRunning(m_capture_interval_sec);
                }
                else
                {
                    sleepAndCheckRunning(m_capture_interval_sec - local_tm->tm_min * 60);
                }
            }
        } // Конец while(running)

        std::cout << "Поток захвата завершил выполнение." << std::endl;
        // Финальная очистка всех возможных временных папок объектов при выходе потока (на всякий случай)
        for (const auto &mapObject : m_mapObjects)
        {
            std::string object_temp_dir_name = screen_temp_directory_name_base + "_" + mapObject.name;
            std::string object_temp_full_path = mapObject.save_directory + "/" + object_temp_dir_name; // Путь зависит от объекта
            std::error_code ec_final;
            std::filesystem::remove_all(object_temp_full_path, ec_final);
            if (ec_final)
            {
                std::cerr << "Ошибка при финальной очистке временного каталога '" << object_temp_full_path << "': " << ec_final.message() << std::endl;
            }
        }
    }

private:
    std::vector<MapObject> m_mapObjects;
    int m_capture_interval_sec;
    std::string m_start_time_str;
    std::string m_end_time_str;
    bool running;

    void sleepAndCheckRunning(int seconds)
    {
        for (int i = 0; i < seconds * 10 && running; ++i)
        { // Проверять каждые 100мс
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    bool isWithinCaptureTimeWindow(const std::tm *current_time_tm)
    {
        if (!current_time_tm)
            return false;

        size_t start_colon = m_start_time_str.find(':');
        size_t end_colon = m_end_time_str.find(':');

        if (start_colon == std::string::npos || end_colon == std::string::npos || m_start_time_str.length() < 4 || m_end_time_str.length() < 4)
        {
            std::cerr << "Неверный формат времени в настройках. Используйте hh:mm." << std::endl;
            return false; // Не продолжать, если формат времени некорректен
        }
        try
        {
            int start_hour = std::stoi(m_start_time_str.substr(0, start_colon));
            int start_minute = std::stoi(m_start_time_str.substr(start_colon + 1));
            int end_hour = std::stoi(m_end_time_str.substr(0, end_colon));
            int end_minute = std::stoi(m_end_time_str.substr(end_colon + 1));

            if (start_hour < 0 || start_hour > 23 || start_minute < 0 || start_minute > 59 ||
                end_hour < 0 || end_hour > 23 || end_minute < 0 || end_minute > 59)
            {
                std::cerr << "Неверное значение часа или минуты в диапазоне времени." << std::endl;
                return false;
            }

            int current_total_minutes = current_time_tm->tm_hour * 60 + current_time_tm->tm_min;
            int start_total_minutes = start_hour * 60 + start_minute;
            int end_total_minutes = end_hour * 60 + end_minute;

            if (start_total_minutes <= end_total_minutes)
            {
                return current_total_minutes >= start_total_minutes && current_total_minutes < end_total_minutes;
            }
            else
            { // Диапазон времени с переходом через полночь
                return current_total_minutes >= start_total_minutes || current_total_minutes < end_total_minutes;
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "Ошибка парсинга времени: " << e.what() << std::endl;
            return false;
        }
        return false;
    }

    void generateCoordinatesForObject(const MapObject &obj, std::vector<std::pair<double, double>> &out_coords)
    {
        out_coords.clear();
        double lat0 = obj.latitude_center;
        double lon0 = obj.longitude_center;
        double r_km = static_cast<double>(obj.radius_km);

        const double deg_per_km_at_equator = 0.01; // Приблизительно градусов на км
        const double step_deg_y = 0.0137;          // Шаг по широте (Y)
        const double step_deg_x = 0.0193;          // Шаг по долготе (X)

        double span_from_center_lat_deg = r_km * deg_per_km_at_equator;
        double cos_lat0 = std::cos(lat0 * M_PI / 180.0);
        if (std::abs(cos_lat0) < std::numeric_limits<double>::epsilon())
        {
            cos_lat0 = std::numeric_limits<double>::epsilon();
        }
        double span_from_center_lon_deg = r_km * deg_per_km_at_equator / cos_lat0;

        int num_steps_from_center_lat = static_cast<int>(std::floor(span_from_center_lat_deg / step_deg_y));
        int num_steps_from_center_lon = static_cast<int>(std::floor(span_from_center_lon_deg / step_deg_x));

        int N_grid = std::max(1, 2 * std::max(num_steps_from_center_lat, num_steps_from_center_lon) + 1);
        if (N_grid == 0)
            N_grid = 1; // Минимум 1x1 сетка

        double start_lat = lat0 - (N_grid - 1) / 2.0 * step_deg_y - step_deg_y / 2.0; // Скорректировано для bbox
        double start_lon = lon0 - (N_grid - 1) / 2.0 * step_deg_x - step_deg_x / 2.0; // Скорректировано для bbox

        std::cout << "Для объекта '" << obj.name << "': Центр(" << lat0 << ", " << lon0 << "), R=" << r_km
                  << "км. Сетка " << N_grid << "x" << N_grid << "." << std::endl;

        for (int yy = 0; yy < N_grid; ++yy)
        {
            for (int xx = 0; xx < N_grid; ++xx)
            {
                // Координаты левого нижнего угла тайла
                double current_tile_lat_bottom = start_lat + yy * step_deg_y;
                double current_tile_lon_left = start_lon + xx * step_deg_x;
                // Для bbox передаем {lat_bottom, lon_left}
                out_coords.push_back({current_tile_lat_bottom, current_tile_lon_left});
            }
        }
    }

    void createSnapshot(std::pair<double, double> bottom_left_coord, const std::string &directory, const std::string &format, int index, std::tm *current_time_tm)
    {
        if (!running)
            return;

        char filename_time_buffer[80];
        std::strftime(filename_time_buffer, sizeof(filename_time_buffer), format.c_str(), current_time_tm);
        std::string file_name = directory + "/" + filename_time_buffer + "_" + std::to_string(index) + ".png";

        if (!std::filesystem::exists(directory))
        {
            std::cerr << "Целевой каталог для снимка не существует: " << directory << ". Пропускаем снимок." << std::endl;
            return;
        }

        double lat_bottom = bottom_left_coord.first;
        double lon_left = bottom_left_coord.second;

        // Определяем размер тайла (примерно 0.01 градуса для Яндекса с spn=0.015,0.015 и size=450,450)
        // Bbox-у Яндекса нужны {lon_left,lat_bottom}~{lon_right,lat_top}
        // Используем фиксированный размер тайла, соответствующий шагам сетки
        const double tile_height_deg = 0.01; // 37; // Это наш step_deg_y
        const double tile_width_deg = 0.01;  // 93;  // Это наш step_deg_x

        double lon_right = lon_left + tile_width_deg;
        double lat_top = lat_bottom + tile_height_deg;

        std::ostringstream oss_api_url;
        oss_api_url.imbue(std::locale("C")); // Для точки в качестве десятичного разделителя
        oss_api_url << "https://static-maps.yandex.ru/1.x/?bbox="
                    << lon_left << "," << lat_bottom << "~" << lon_right << "," << lat_top
                    << "&size=450,450&l=map,trf"; // trf - пробки

        std::string api_url = oss_api_url.str();

        // std::cout << "Запрос URL: " << api_url << std::endl; // Для отладки

        CURL *curl = curl_easy_init();
        if (curl)
        {
            FILE *file = fopen(file_name.c_str(), "wb");
            if (file)
            {
                curl_easy_setopt(curl, CURLOPT_URL, api_url.c_str());
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
                curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
                // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L); // Для отладки CURL
                CURLcode res = curl_easy_perform(curl);
                long http_code = 0;
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
                fclose(file);

                if (res == CURLE_OK && http_code >= 200 && http_code < 300)
                {
                    std::ifstream ifs(file_name, std::ios::binary | std::ios::ate);
                    if (!ifs.is_open() || ifs.tellg() == 0)
                    {
                        std::cerr << "Ошибка: Загруженный файл пуст или не удалось проверить: " << file_name << std::endl;
                        std::filesystem::remove(file_name);
                    }
                    else
                    {
                        // std::cout << "Снимок сохранен: " << file_name << std::endl; // Слишком много логов
                    }
                }
                else
                {
                    std::cerr << "Ошибка CURL (код: " << res << ", HTTP: " << http_code << ") для URL " << api_url << ": " << curl_easy_strerror(res) << std::endl;
                    std::filesystem::remove(file_name); // Удалить файл при ошибке
                }
            }
            else
            {
                std::cerr << "Ошибка открытия файла для записи: " << file_name << " (errno: " << errno << ")" << std::endl;
            }
            curl_easy_cleanup(curl);
        }
        else
        {
            std::cerr << "Ошибка инициализации CURL." << std::endl;
        }
    }
};

// Основное окно приложения
class SnapshotApp : public QWidget
{
    Q_OBJECT

public:
    SnapshotApp(QWidget *parent = nullptr) : QWidget(parent), captureThread(nullptr)
    {
        setWindowTitle("Снимки карты по объектам");
        QVBoxLayout *mainLayout = new QVBoxLayout(this);

        // --- Панель добавления объекта ---
        QGroupBox *addObjectGroup = new QGroupBox("Добавить объект на карту");
        QVBoxLayout *objectGroupLayout = new QVBoxLayout();

        objectNameEdit = new QLineEdit("Объект1");
        objectGroupLayout->addWidget(new QLabel("Имя объекта (уникальное):"));
        objectGroupLayout->addWidget(objectNameEdit);

        latEdit = new QLineEdit("45.07");
        QDoubleValidator *latValidator = new QDoubleValidator(-90.0, 90.0, 8, this);
        latValidator->setLocale(QLocale::c());
        latEdit->setValidator(latValidator);
        objectGroupLayout->addWidget(new QLabel("Широта центра:"));
        objectGroupLayout->addWidget(latEdit);

        lonEdit = new QLineEdit("39.0");
        QDoubleValidator *lonValidator = new QDoubleValidator(-180.0, 180.0, 8, this);
        lonValidator->setLocale(QLocale::c());
        lonEdit->setValidator(lonValidator);
        objectGroupLayout->addWidget(new QLabel("Долгота центра:"));
        objectGroupLayout->addWidget(lonEdit);

        radiusEdit = new QLineEdit("5"); // 5 км по умолчанию
        radiusEdit->setValidator(new QIntValidator(1, 1000, this));
        objectGroupLayout->addWidget(new QLabel("Радиус съемки объекта (км):"));
        objectGroupLayout->addWidget(radiusEdit);

        // Поле для указания пути сохранения для ЭТОГО объекта
        objectSaveDirEdit = new QLineEdit("./screenshots_output/Объект1"); // Путь по умолчанию
        objectGroupLayout->addWidget(new QLabel("Путь для сохранения снимков объекта:"));
        QHBoxLayout *objectDirLayout = new QHBoxLayout();
        objectDirLayout->addWidget(objectSaveDirEdit);
        QPushButton *browseObjectDirButton = new QPushButton("Обзор...");
        // Подключаем к слоту, который будет обновлять только это поле
        connect(browseObjectDirButton, &QPushButton::clicked, this, &SnapshotApp::browseObjectDirectory);
        objectDirLayout->addSpacing(5);
        objectDirLayout->addWidget(browseObjectDirButton);
        objectGroupLayout->addLayout(objectDirLayout);

        addMapObjectButton = new QPushButton("Добавить объект");
        connect(addMapObjectButton, &QPushButton::clicked, this, &SnapshotApp::onAddMapObject);
        objectGroupLayout->addWidget(addMapObjectButton);

        addObjectGroup->setLayout(objectGroupLayout);
        mainLayout->addWidget(addObjectGroup);

        // --- Список объектов ---
        mapObjectsListWidget = new QListWidget();
        mainLayout->addWidget(new QLabel("Список объектов для съемки:"));
        mainLayout->addWidget(mapObjectsListWidget);
        removeMapObjectButton = new QPushButton("Удалить выбранный объект");
        connect(removeMapObjectButton, &QPushButton::clicked, this, &SnapshotApp::onRemoveMapObject);
        mainLayout->addWidget(removeMapObjectButton);

        // --- Общие настройки (без пути сохранения) ---
        QGroupBox *settingsGroup = new QGroupBox("Общие настройки съемки");
        QVBoxLayout *settingsLayout = new QVBoxLayout();

        startTimeEdit = new QTimeEdit(QTime::fromString("07:00", "hh:mm"));
        startTimeEdit->setDisplayFormat("hh:mm");
        settingsLayout->addWidget(new QLabel("Время начала (hh:mm):"));
        settingsLayout->addWidget(startTimeEdit);

        endTimeEdit = new QTimeEdit(QTime::fromString("23:00", "hh:mm"));
        endTimeEdit->setDisplayFormat("hh:mm");
        settingsLayout->addWidget(new QLabel("Время окончания (hh:mm):"));
        settingsLayout->addWidget(endTimeEdit);

        intervalEdit = new QLineEdit("60");                           // 1 час
        intervalEdit->setValidator(new QIntValidator(1, 1440, this)); // от 1 мин до 24 часов
        settingsLayout->addWidget(new QLabel("Интервал съемки (м):"));
        settingsLayout->addWidget(intervalEdit);

        settingsGroup->setLayout(settingsLayout);
        mainLayout->addWidget(settingsGroup);

        // --- Кнопки управления ---
        QHBoxLayout *controlButtonsLayout = new QHBoxLayout();
        startButton = new QPushButton("Начать съемку");
        connect(startButton, &QPushButton::clicked, this, &SnapshotApp::startCapture);
        controlButtonsLayout->addWidget(startButton);

        stopButton = new QPushButton("Остановить съемку");
        connect(stopButton, &QPushButton::clicked, this, &SnapshotApp::stopCapture);
        controlButtonsLayout->addWidget(stopButton);
        mainLayout->addLayout(controlButtonsLayout);

        setLayout(mainLayout);

        // Обновление пути сохранения объекта при изменении имени
        connect(objectNameEdit, &QLineEdit::textChanged, this, &SnapshotApp::updateObjectSaveDir);

        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    ~SnapshotApp()
    {
        stopCapture(); // Остановить поток, если он работает
        if (captureThread)
        {
            captureThread->wait(5000); // Ждать завершения
            delete captureThread;      // Удалить после wait
            captureThread = nullptr;
        }
        curl_global_cleanup();
    }

private slots:
    void onAddMapObject()
    {
        bool ok_lat, ok_lon, ok_radius;
        QString name_str = objectNameEdit->text().trimmed();
        double lat = latEdit->text().toDouble(&ok_lat);
        double lon = lonEdit->text().toDouble(&ok_lon);
        int radius_val = radiusEdit->text().toInt(&ok_radius);
        QString save_dir_str = objectSaveDirEdit->text().trimmed();

        if (name_str.isEmpty())
        {
            QMessageBox::warning(this, "Ошибка", "Имя объекта не может быть пустым.");
            return;
        }
        // Проверка уникальности имени
        for (const auto &item : m_mapObjectList)
        {
            if (item.name == name_str.toStdString())
            {
                QMessageBox::warning(this, "Ошибка", "Объект с таким именем уже существует.");
                return;
            }
        }

        if (!ok_lat || !ok_lon || !ok_radius || radius_val <= 0)
        {
            QMessageBox::warning(this, "Ошибка", "Неверные параметры для объекта (широта, долгота или радиус).");
            return;
        }
        if (save_dir_str.isEmpty())
        {
            QMessageBox::warning(this, "Ошибка", "Путь для сохранения объекта не может быть пустым.");
            return;
        }

        MapObject newObj(lat, lon, radius_val, name_str.toStdString(), save_dir_str.toStdString());
        m_mapObjectList.push_back(newObj);
        mapObjectsListWidget->addItem(newObj.getDisplayText());

        // Подготовка полей для следующего объекта
        int next_obj_num = 1;
        if (name_str.startsWith("Объект") && name_str.mid(6).toInt() > 0)
        {
            next_obj_num = name_str.mid(6).toInt() + 1;
        }
        else
        {
            next_obj_num = m_mapObjectList.size() + 1;
        }
        objectNameEdit->setText("Объект" + QString::number(next_obj_num));
        // Обновляем путь сохранения для нового имени
        updateObjectSaveDir(objectNameEdit->text());
        // Очистить другие поля или установить значения по умолчанию
        latEdit->setText("0.0"); // Или другие дефолтные значения
        lonEdit->setText("0.0");
        radiusEdit->setText("5");

        std::cout << "Добавлен объект: " << newObj.name << " в каталог " << newObj.save_directory << std::endl;
    }

    void onRemoveMapObject()
    {
        int currentRow = mapObjectsListWidget->currentRow();
        if (currentRow >= 0 && currentRow < static_cast<int>(m_mapObjectList.size()))
        {
            std::cout << "Удаление объекта: " << m_mapObjectList[currentRow].name << std::endl;
            m_mapObjectList.erase(m_mapObjectList.begin() + currentRow);
            delete mapObjectsListWidget->takeItem(currentRow); // Удалить из GUI
            std::cout << "Удален объект с индексом " << currentRow << std::endl;
        }
        else
        {
            QMessageBox::information(this, "Информация", "Выберите объект для удаления.");
        }
    }

    void startCapture()
    {
        if (captureThread && captureThread->isRunning())
        {
            QMessageBox::warning(this, "Предупреждение", "Съемка уже запущена.");
            return;
        }

        if (m_mapObjectList.empty())
        {
            QMessageBox::warning(this, "Предупреждение", "Список объектов для съемки пуст. Добавьте хотя бы один объект.");
            return;
        }

        bool ok_interval;
        std::string current_start_time = startTimeEdit->time().toString("hh:mm").toStdString();
        std::string current_end_time = endTimeEdit->time().toString("hh:mm").toStdString();
        int current_capture_interval = intervalEdit->text().toInt(&ok_interval) * 60;

        if (!ok_interval || current_capture_interval < 60)
        { // Минимальный интервал 60с
            QMessageBox::critical(this, "Ошибка", "Неверное значение интервала съемки (минимум 60 секунд).");
            return;
        }

        // Проверка путей сохранения объектов перед запуском
        for (const auto &obj : m_mapObjectList)
        {
            if (obj.save_directory.empty())
            {
                QMessageBox::critical(this, "Ошибка", QString("Путь сохранения для объекта '%1' пуст.").arg(QString::fromStdString(obj.name)));
                return;
            }
            // Можно добавить проверку на доступность записи в каталог, но создание каталогов в потоке надежнее.
        }

        // Создать и запустить поток
        if (captureThread)
        {                              // Если предыдущий поток существует, но не запущен
            captureThread->wait(1000); // Дать немного времени на завершение, если он в процессе остановки
            delete captureThread;
            captureThread = nullptr;
        }

        // Передаем вектор объектов в поток
        captureThread = new CaptureThread(m_mapObjectList,
                                          current_capture_interval,
                                          current_start_time,
                                          current_end_time,
                                          this); // parent
        connect(captureThread, &QThread::finished, captureThread, &QObject::deleteLater);
        connect(captureThread, &QThread::finished, this, [this]()
                {
            this->startButton->setEnabled(true);
            this->stopButton->setEnabled(false);
            this->addMapObjectButton->setEnabled(true);
            this->removeMapObjectButton->setEnabled(true);
            QMessageBox::information(this, "Статус", "Процесс съемки завершен."); });

        captureThread->start();
        startButton->setEnabled(false); // Disable start button while running
        stopButton->setEnabled(true);
        addMapObjectButton->setEnabled(false);
        removeMapObjectButton->setEnabled(false);

        QMessageBox::information(this, "Статус", "Съемка начата.");
    }

    void stopCapture()
    {
        if (captureThread && captureThread->isRunning())
        {
            std::cout << "Нажата кнопка 'Остановить'. Сигнализируем потоку." << std::endl;
            captureThread->stop();
            stopButton->setEnabled(false); // Disable stop button immediately
            // startButton re-enabled via finished signal
            QMessageBox::information(this, "Статус", "Запрос на остановку съемки отправлен. Поток завершит текущую операцию и остановится.");
        }
        else
        {
            if (!captureThread)
            { // if thread was never started or already deleted
                startButton->setEnabled(true);
                stopButton->setEnabled(false);
                addMapObjectButton->setEnabled(true);
                removeMapObjectButton->setEnabled(true);
            }
            QMessageBox::information(this, "Информация", "Съемка не запущена.");
        }
    }

    // Слот для выбора каталога сохранения для текущего добавляемого объекта
    void browseObjectDirectory()
    {
        QString currentObjectDir = objectSaveDirEdit->text().trimmed();
        if (currentObjectDir.isEmpty())
        {
            currentObjectDir = "./screenshots_output/" + objectNameEdit->text().trimmed(); // Предлагаем путь по имени
        }

        QString directory = QFileDialog::getExistingDirectory(this, "Выбор каталога для объекта", currentObjectDir);

        if (!directory.isEmpty())
        {
            objectSaveDirEdit->setText(directory);
        }
    }

    // Слот для обновления предлагаемого пути сохранения при изменении имени объекта
    void updateObjectSaveDir(const QString &name)
    {
        // Обновляем предлагаемый путь сохранения на основе нового имени объекта
        // Это не меняет путь у уже добавленных объектов
        QString base_dir = "./screenshots_output/"; // Или другой базовый путь по умолчанию
        QString safe_name = name.trimmed();
        if (safe_name.isEmpty())
            safe_name = "Объект";
        // Убрать недопустимые символы для имени папки (просто пример, можно улучшить)
        safe_name.replace(" ", "_");
        safe_name.replace("/", "_");
        // Добавьте другие символы, если нужно

        objectSaveDirEdit->setText(base_dir + safe_name);
    }

private:
    // Элементы GUI для добавления объекта
    QLineEdit *objectNameEdit;
    QLineEdit *latEdit;
    QLineEdit *lonEdit;
    QLineEdit *radiusEdit;
    QLineEdit *objectSaveDirEdit; // Поле для пути сохранения объекта
    QPushButton *addMapObjectButton;

    // Список объектов
    QListWidget *mapObjectsListWidget;
    QPushButton *removeMapObjectButton;
    std::vector<MapObject> m_mapObjectList; // Хранилище объектов

    // Общие настройки
    QTimeEdit *startTimeEdit;
    QTimeEdit *endTimeEdit;
    QLineEdit *intervalEdit;

    // Кнопки управления
    QPushButton *startButton;
    QPushButton *stopButton;

    CaptureThread *captureThread;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    std::locale::global(std::locale("C")); // Для корректного преобразования чисел в строки (точка как разделитель)

    // Создать базовый каталог для скриншотов по умолчанию, если он не существует
    std::string base_screenshot_dir = "./screenshots_output";
    std::error_code ec;
    if (!std::filesystem::exists(base_screenshot_dir))
    {
        std::filesystem::create_directories(base_screenshot_dir, ec);
        if (ec)
        {
            std::cerr << "Предупреждение: Не удалось создать базовый каталог для скриншотов '" << base_screenshot_dir << "': " << ec.message() << std::endl;
            // Приложение продолжит работу, но пользователь должен будет выбрать доступный каталог для каждого объекта.
        }
    }

    SnapshotApp window;
    window.resize(500, 750); // Немного увеличим окно
    window.show();

    return app.exec();
}

#include "main.moc"