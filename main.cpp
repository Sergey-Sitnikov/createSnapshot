#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QTimeEdit>
#include <QPushButton>
#include <QMessageBox>
#include <QFileDialog>
#include <QThread>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <curl/curl.h>
#include <ctime>
#include <fstream>
#include <chrono>
#include <thread>
#include <cmath>     // Для std::cos, std::floor, M_PI, std::sqrt
#include <limits>    // Для std::numeric_limits
#include <cstdio>    // Для snprintf
#include <algorithm> // Для std::max, std::sort
#include <vector>    // Для std::vector
#include <cerrno>    // Для ошибок filesystem
#include <locale>    // Для std::locale (используется, но может быть избыточным с snprintf)

// Qt Image/Painter/Dir includes
#include <QImage>
#include <QPainter>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QFile>
#include <QtGlobal> // Для qSqrt (если нужно, но std::sqrt из cmath достаточно)

// Глобальные переменные с параметрами
std::vector<std::pair<double, double>> coordinat; // Координаты для скриншотов
// coordinat_centr будет считываться из GUI
int capture_interval = 3600; // Интервал в секундах
std::string start_time = "07:00";
std::string end_time = "23:00";
int radius = 5; // Радиус в км (значение по умолчанию)

// Путь по умолчанию для сохранения окончательных скриншотов
std::string screenshot_directory = "./screenshots";
// Название временной папки
const std::string screen_temp_directory_name = "screen_temp";

// Вспомогательная функция для объединения изображений из временного каталога в один BMP файл.
// Она обрабатывает файлы из temp_dir и сохраняет результат в output_dir.
// Горизонтальные ряды объединяются в обратном порядке.
// Возвращает true в случае успеха, false в случае неудачи.
bool combineAndCleanupScreenshots(const std::string &temp_dir_path, const std::string &output_dir_path, std::tm *current_time)
{
    QDir tempDir(QString::fromStdString(temp_dir_path));
    if (!tempDir.exists())
    {
        std::cerr << "Временный каталог для объединения не существует: " << temp_dir_path << std::endl;
        // Попытка очистки с помощью filesystem на случай, если представление QDir устарело или каталог существует частично
        std::error_code ec;
        std::filesystem::remove_all(temp_dir_path, ec);
        if (ec)
        {
            std::cerr << "Ошибка при очистке несуществующего временного каталога с помощью filesystem: " << ec.message() << std::endl;
        }
        return false;
    }

    QStringList filters;
    filters << "*.png"; // Предполагается, что временные файлы являются PNG из Yandex API

    // Получить список файлов и отсортировать их по индексу, встроенному в имя файла
    QFileInfoList fileList = tempDir.entryInfoList(filters, QDir::Files, QDir::Name);

    if (fileList.isEmpty())
    {
        std::cerr << "Временные скриншоты в каталоге " << temp_dir_path << " не найдены. Объединение пропущено." << std::endl;
        // Очистить пустой временный каталог с помощью filesystem
        std::error_code ec;
        std::filesystem::remove_all(temp_dir_path, ec);
        if (ec)
        {
            std::cerr << "Ошибка при очистке пустого временного каталога с помощью filesystem: " << ec.message() << std::endl;
        }
        return false;
    }

    // Сортировка файлов по индексу, встроенному в имя файла
    // Ожидается формат имени файла: "Скриншот_YYYY-MM-DD_HH-MM-SS_X.png", где X - индекс
    std::sort(fileList.begin(), fileList.end(), [](const QFileInfo &a, const QFileInfo &b)
              {
        QString name_a = a.baseName();
        QString name_b = b.baseName();
        int last_underscore_a = name_a.lastIndexOf('_');
        int last_underscore_b = name_b.lastIndexOf('_');

        int index_a = -1, index_b = -1; // Использовать -1 для неверного индекса

        if (last_underscore_a != -1) {
            index_a = name_a.mid(last_underscore_a + 1).toInt();
        }
        if (last_underscore_b != -1) {
            index_b = name_b.mid(last_underscore_b + 1).toInt();
        }

        return index_a < index_b; });

    // Определить размер сетки N по количеству файлов
    int total_images = fileList.size();
    int N = static_cast<int>(std::sqrt(total_images));
    // Проверить, является ли total_images полным квадратом
    if (N * N != total_images)
    {
        std::cerr << "Ошибка: Общее количество скриншотов (" << total_images << ") не является полным квадратом (" << N << "x" << N << "). Невозможно сформировать сетку. Временные файлы сохранены для проверки в " << temp_dir_path << std::endl;
        // В этом конкретном случае ошибки НЕ очищать временные файлы, пользователь может захотеть их увидеть.
        return false;
    }

    // Загрузить первое изображение, чтобы получить размеры (предполагается, что все изображения имеют одинаковые размеры)
    QImage firstImage(fileList.first().absoluteFilePath());
    if (firstImage.isNull())
    {
        std::cerr << "Ошибка загрузки первого изображения: " << fileList.first().absoluteFilePath().toStdString() << ". Невозможно определить размеры." << std::endl;
        // Очистить временные файлы с помощью filesystem
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

    // Создать холст для окончательного композитного изображения
    int composite_width = N * img_width;
    int composite_height = N * img_height;

    // Использовать формат RGB32, подходящий для сохранения в BMP
    QImage compositeImage(composite_width, composite_height, QImage::Format_RGB32);
    compositeImage.fill(Qt::white); // Залить белым фоном

    QPainter painter(&compositeImage);
    painter.setRenderHint(QPainter::Antialiasing, false); // Сглаживание не требуется для отрисовки изображений

    // Отрисовать каждое изображение на композитный холст в соответствующей позиции сетки
    for (int i = 0; i < total_images; ++i)
    {
        QImage img(fileList.at(i).absoluteFilePath());
        if (img.isNull())
        {
            std::cerr << "Ошибка загрузки изображения: " << fileList.at(i).absoluteFilePath().toStdString() << ". Пропускаем отрисовку этого файла." << std::endl;
            // Решите, как обрабатывать отсутствующие изображения в композите - оставить пустое место - один из вариантов.
            continue;
        }

        // Вычислить позицию в сетке (строка и столбец) из индекса
        int row = i / N; // Строка на основе отсортированного индекса (от 0 до N-1)
        int col = i % N; // Столбец на основе отсортированного индекса (от 0 до N-1)

        // Вычислить индекс строки для композитного изображения, чтобы инвертировать горизонтальный порядок
        // Первая исходная строка (индекс 0) попадает в последнюю композитную строку (индекс N-1)
        // Последняя исходная строка (индекс N-1) попадает в первую композитную строку (индекс 0)
        int composite_row = (N - 1) - row;

        painter.drawImage(col * img_width, composite_row * img_height, img); // Использовать composite_row для Y-координаты
    }

    painter.end();

    // Сформировать имя выходного файла с меткой времени в конечном выходном каталоге
    char date_buffer[80];
    std::strftime(date_buffer, sizeof(date_buffer), "Снимок_сетки_%Y-%m-%d_%H-%M-%S.bmp", current_time);
    std::string output_file_name = output_dir_path + "/" + date_buffer;

    // Сохранить композитное изображение как BMP
    if (compositeImage.save(QString::fromStdString(output_file_name), "BMP"))
    {
        std::cout << "Композитный скриншот сохранен: " << output_file_name << std::endl;
        // Очистить временные файлы после успешного сохранения с помощью filesystem
        std::error_code ec;
        std::filesystem::remove_all(temp_dir_path, ec);
        if (ec)
        {
            std::cerr << "Ошибка при очистке filesystem после успешного сохранения композита: " << ec.message() << std::endl;
        }
        else
        {
            std::cout << "Временный каталог очищен: " << temp_dir_path << std::endl;
        }
        return true;
    }
    else
    {
        std::cerr << "Ошибка сохранения композитного скриншота: " << output_file_name << std::endl;
        // Очистить временные файлы независимо от успешности сохранения композита, согласно запросу пользователя
        std::error_code ec;
        std::filesystem::remove_all(temp_dir_path, ec);
        if (ec)
        {
            std::cerr << "Ошибка при очистке filesystem после неудачной попытки сохранения композита: " << ec.message() << std::endl;
        }
        else
        {
            std::cout << "Временный каталог очищен после попытки сохранения: " << temp_dir_path << std::endl;
        }
        return false;
    }
}

// Поток для выполнения захвата скриншотов
class CaptureThread : public QThread
{
    Q_OBJECT

public:
    CaptureThread(QObject *parent = nullptr) : QThread(parent), running(true) {}

    void setDirectory(const std::string &directory) { saving_directory = directory; }

    // Метод для безопасной остановки потока
    void stop()
    {
        running = false;
        // Сигнализировать потоку проснуться, если он спит, хотя короткие задержки уже позволяют это.
        // Если бы это была долгая блокирующая операция, потребовались бы условие ожидания или событие.
    }

    void run() override
    {
        running = true;
        // Сформировать полный путь к временному каталогу внутри выбранного пользователем каталога
        std::string temp_save_dir = saving_directory + "/" + screen_temp_directory_name;

        // Убедиться, что временный каталог чист в самом начале жизни потока
        // Использовать std::filesystem для надежного удаления и создания
        std::error_code ec;
        std::filesystem::remove_all(temp_save_dir, ec);
        if (ec)
        {
            std::cerr << "Ошибка при начальной очистке временного каталога '" << temp_save_dir << "' с помощью filesystem: " << ec.message() << std::endl;
        }
        // Убедиться, что временный каталог существует перед началом цикла
        if (!std::filesystem::exists(temp_save_dir))
        {
            std::filesystem::create_directory(temp_save_dir, ec);
            if (ec)
            {
                std::cerr << "Ошибка создания временного каталога '" << temp_save_dir << "': " << ec.message() << std::endl;
                // Если временный каталог не может быть создан, поток не может работать правильно
                running = false; // Остановить поток, если создание временного каталога не удалось
                return;          // Выйти из метода run
            }
        }

        while (running)
        {
            std::time_t now_t = std::time(nullptr);
            std::tm *current_time = std::localtime(&now_t);

            // Проверить, действителен ли current_time перед использованием
            if (!current_time)
            {
                std::cerr << "Ошибка получения текущего времени. Пропускаем цикл захвата." << std::endl;
                // Подождать немного перед повторной попыткой, чтобы избежать бесконечного цикла при ошибке
                std::this_thread::sleep_for(std::chrono::seconds(10));
                continue;
            }

            // Разобрать строки времени в целые числа для сравнения
            // Базовая проверка на действительность формата hh:mm перед stoi
            size_t start_colon = start_time.find(':');
            size_t end_colon = end_time.find(':');
            if (start_colon == std::string::npos || end_colon == std::string::npos || start_time.length() < 5 || end_time.length() < 5)
            {
                std::cerr << "Неверный формат времени. Пожалуйста, используйте hh:mm. Пропускаем цикл захвата." << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(60)); // Подождать минуту перед повторной проверкой формата времени
                continue;
            }

            int start_hour = std::stoi(start_time.substr(0, start_colon));
            int start_minute = std::stoi(start_time.substr(start_colon + 1, 2));
            int end_hour = std::stoi(end_time.substr(0, end_colon));
            int end_minute = std::stoi(end_time.substr(end_colon + 1, 2));

            // Базовая проверка значений часа/минуты
            if (start_hour < 0 || start_hour > 23 || start_minute < 0 || start_minute > 59 ||
                end_hour < 0 || end_hour > 23 || end_minute < 0 || end_minute > 59)
            {
                std::cerr << "Неверное значение часа или минуты в диапазоне времени. Пропускаем цикл захвата." << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(60)); // Подождать минуту
                continue;
            }

            // Более точная проверка времени, включая минуты
            bool is_within_time = false;
            int current_total_minutes = current_time->tm_hour * 60 + current_time->tm_min;
            int start_total_minutes = start_hour * 60 + start_minute;
            int end_total_minutes = end_hour * 60 + end_minute;

            if (start_total_minutes <= end_total_minutes)
            { // Обычный диапазон времени (например, с 07:00 до 23:00)
                if (current_total_minutes >= start_total_minutes && current_total_minutes < end_total_minutes)
                {
                    is_within_time = true;
                }
            }
            else
            { // Диапазон времени с переходом через полночь (например, с 22:00 до 06:00)
                if (current_total_minutes >= start_total_minutes || current_total_minutes < end_total_minutes)
                {
                    is_within_time = true;
                }
            }

            if (!is_within_time)
            {
                // Подождать до времени начала или проверить снова через короткий интервал
                auto start_sleep_chrono = std::chrono::high_resolution_clock::now();
                // std::cout << "Вне времени захвата (" << start_time << " - " << end_time << "). Ожидаем..." << std::endl; // Слишком много сообщений
                // Подождать короткий интервал (например, 5 минут) перед повторной проверкой времени
                while (running && std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - start_sleep_chrono).count() < 300)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Короткие задержки позволяют быстро остановить
                }
                continue; // Проверить время снова после задержки
            }

            // Внутри диапазона времени захвата
            std::cout << "Внутри времени захвата (" << start_time << " - " << end_time << "). Запуск пакета захвата." << std::endl;

            if (coordinat.empty())
            {
                std::cerr << "Список координат пуст. Захват невозможен." << std::endl;
                // Ждать интервал захвата, даже если нет координат, чтобы избежать бесконечного цикла
                auto start_sleep_chrono = std::chrono::high_resolution_clock::now();
                while (running && std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - start_sleep_chrono).count() < capture_interval)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                continue;
            }

            // Очистить временный каталог перед началом нового пакета захватов
            std::filesystem::remove_all(temp_save_dir, ec);
            if (ec)
            {
                std::cerr << "Ошибка при очистке временного каталога перед новым пакетом '" << temp_save_dir << "' с помощью filesystem: " << ec.message() << std::endl;
            }
            // Убедиться, что он существует для этого пакета захвата
            if (!std::filesystem::exists(temp_save_dir))
            {
                std::filesystem::create_directory(temp_save_dir, ec);
                if (ec)
                {
                    std::cerr << "Ошибка создания временного каталога '" << temp_save_dir << "' перед пакетом: " << ec.message() << std::endl;
                    // Невозможно продолжить захват, если временный каталог не удалось создать
                    auto start_sleep_chrono = std::chrono::high_resolution_clock::now();
                    while (running && std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - start_sleep_chrono).count() < capture_interval)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                    continue; // Попробовать снова в следующем интервале
                }
            }

            // Захватить все отдельные скриншоты во временный каталог
            bool capture_loop_completed = true; // Флаг для проверки, полностью ли выполнился цикл
            std::cout << "Захват отдельных скриншотов в каталог: " << temp_save_dir << std::endl;
            for (size_t u = 0; u < coordinat.size(); ++u)
            {
                if (!running)
                {
                    std::cout << "Запрошена остановка во время цикла захвата отдельных скриншотов." << std::endl;
                    capture_loop_completed = false; // Цикл был прерван
                    break;                          // Выйти из цикла захвата
                }
                // Получить текущее время для имени файла этого конкретного снимка
                std::time_t now_t_inner = std::time(nullptr);
                std::tm *current_time_inner = std::localtime(&now_t_inner);
                createSnapshot(coordinat[u], temp_save_dir, "Скриншот_%Y-%m-%d_%H-%M-%S", static_cast<int>(u), current_time_inner);
            }

            // --- Обработка после захвата отдельных скриншотов ---
            if (capture_loop_completed)
            {
                // Если цикл захвата завершился без остановки, объединить изображения
                std::cout << "Пакет захвата завершен. Объединение скриншотов..." << std::endl;
                // Использовать метку времени из конца пакета (текущее время) для имени объединенного файла
                std::time_t now_t_batch = std::time(nullptr);
                std::tm *current_time_batch = std::localtime(&now_t_batch);
                combineAndCleanupScreenshots(temp_save_dir, saving_directory, current_time_batch);
                std::cout << "Объединение завершено." << std::endl;
            }
            else
            {
                // Если цикл был прерван запросом на остановку, немедленно очистить частичные результаты
                std::cerr << "Пакет захвата прерван. Очистка частичных временных файлов." << std::endl;
                std::error_code ec_partial;
                std::filesystem::remove_all(temp_save_dir, ec_partial);
                if (ec_partial)
                {
                    std::cerr << "Ошибка при очистке частичного временного каталога '" << temp_save_dir << "' с помощью filesystem: " << ec_partial.message() << std::endl;
                }
                else
                {
                    std::cout << "Частичный временный каталог очищен: " << temp_save_dir << std::endl;
                }
            }
            // --- Конец обработки после захвата отдельных скриншотов ---

            // Ждать следующий интервал захвата. Ждать только если поток все еще запущен и цикл завершен.
            // Если цикл был прерван (capture_loop_completed == false), поток должен немедленно выйти из внешнего цикла.
            if (running && capture_loop_completed)
            {
                auto start_sleep_chrono = std::chrono::high_resolution_clock::now();
                std::cout << "Ожидание следующего интервала захвата (" << capture_interval << "с)..." << std::endl;
                while (running && std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - start_sleep_chrono).count() < capture_interval)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Короткие задержки позволяют быстро остановить
                }
                if (!running)
                {
                    std::cout << "Запрошена остановка во время ожидания интервала." << std::endl;
                }
            }
            else if (!running)
            {
                // Если остановлено, выйти из внешнего цикла while немедленно после возможной частичной очистки
                std::cout << "Выход из цикла run из-за запроса на остановку после прерванного захвата." << std::endl;
            } // Если running true, но capture_loop_completed false, что-то пошло не так, но это не запрос на остановку?
              // Этот случай, в идеале, не должен происходить с текущей логикой.

        } // Конец цикла while(running)

        std::cout << "Поток захвата завершил выполнение." << std::endl;
        // Окончательная очистка временного каталога при выходе потока
        std::string final_temp_dir_path_exit = saving_directory + "/" + screen_temp_directory_name;
        std::error_code ec_final;
        std::filesystem::remove_all(final_temp_dir_path_exit, ec_final);
        if (ec_final)
        {
            std::cerr << "Ошибка при окончательной очистке временного каталога '" << final_temp_dir_path_exit << "' при выходе потока с помощью filesystem: " << ec_final.message() << std::endl;
        }
        else
        {
            std::cout << "Окончательный временный каталог очищен при выходе потока: " << final_temp_dir_path_exit << std::endl;
        }
    } // Конец run()

private:
    std::string saving_directory;
    bool running; // Флаг для управления выполнением потока

    // Метод для создания отдельного снимка через Yandex Static Maps API
    void createSnapshot(std::pair<double, double> url, const std::string &directory, const std::string &format, int index, std::tm *current_time)
    {
        char buffer[80];
        // Использование gmtime вместо localtime для метки времени UTC может быть более согласованным, но localtime подходит для локальных имен файлов
        std::strftime(buffer, sizeof(buffer), format.c_str(), current_time);
        // Сохранить в указанный каталог (который является временным каталогом в run())
        std::string file_name = directory + "/" + buffer + "_" + std::to_string(index) + ".png";

        // Проверить, существует ли целевой каталог (теперь должен быть гарантирован методом run())
        if (!std::filesystem::exists(directory))
        {
            std::cerr << "Целевой каталог для снимка не существует: " << directory << ". Пропускаем снимок." << std::endl;
            return;
        }

        // Убедиться, что locale установлено в "C" для стандартного форматирования десятичной точки (точка)
        // Это критически важно для std::to_string, используемого ниже, хотя snprintf был безопаснее.
        // Давайте вернем snprintf, который не зависит от locale.
        //  std::locale::global(std::locale("C"));
        // std::cout << url.first << " " << url.second << std::endl;
        // std::cout << url.first + 0.02 << " " << url.second + 0.02 << std::endl;

        // std::string api_url = "https://static-maps.yandex.ru/1.x/?ll=" +
        //                       std::to_string(url.second) + "," + std::to_string(url.first) +
        //                       "&spn=0.015,0.015&size=450,450&l=map,trf";

        std::string api_url = "https://static-maps.yandex.ru/1.x/?bbox=" +
                              std::to_string(url.second) + "," + std::to_string(url.first) +
                              "~" + std::to_string(url.second + 0.01) + "," + std::to_string(url.first + 0.01) +
                              "&size=450,450&l=map,trf";
        // std::cout << "Request URL: " << api_url << std::endl; // Для отладки

        CURL *curl = curl_easy_init();
        if (curl)
        {
            FILE *file = fopen(file_name.c_str(), "wb");
            if (file)
            {
                curl_easy_setopt(curl, CURLOPT_URL, api_url.c_str());
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
                curl_easy_setopt(curl, CURLOPT_HEADER, 0L);
                curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L); // Сбой при HTTP ошибках (>= 400)
                curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);     // Установить в 1L для подробного вывода CURL

                CURLcode res = curl_easy_perform(curl);
                long http_code = 0;
                curl_easy_getinfo(curl, CURLINFO_HTTP_CODE, &http_code); // Получить код статуса HTTP

                fclose(file); // Закрыть файл независимо от результата CURL

                if (res == CURLE_OK && http_code >= 200 && http_code < 300)
                {
                    // Проверить, был ли файл действительно загружен и не пуст
                    std::ifstream ifs(file_name, std::ios::binary | std::ios::ate);
                    if (ifs.is_open() && ifs.tellg() > 0)
                    {
                        // Файл действителен и не пуст
                        // std::cout << "Снимок успешно сохранен: " << file_name << std::endl; // Слишком много сообщений
                        // emit snapshotDone(QString::fromStdString(file_name)); // Можно испустить сигнал, если нужно в GUI
                    }
                    else
                    {
                        std::cerr << "Ошибка: Загруженный файл пуст или не удалось повторно открыть -> " << file_name << std::endl;
                        // Удалить пустой или недействительный файл
                        std::filesystem::remove(file_name);
                    }
                }
                else
                {
                    std::cerr << "Ошибка CURL (код: " << res << ", HTTP: " << http_code << ") во время загрузки с " << api_url << ": " << curl_easy_strerror(res) << std::endl;
                    // Удалить файл, если произошла ошибка CURL или HTTP
                    std::filesystem::remove(file_name);
                }
            }
            else
            {
                std::cerr << "Ошибка открытия файла для записи: " << file_name << " (errno: " << errno << ")" << std::endl;
            }
            curl_easy_cleanup(curl); // Очистить easy handle CURL
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
    SnapshotApp(QWidget *parent = nullptr) : QWidget(parent)
    {
        setWindowTitle("Снимки карты");

        QVBoxLayout *layout = new QVBoxLayout(this);

        // Установка времени
        startTimeEdit = new QTimeEdit(QTime::fromString(QString::fromStdString(start_time), "hh:mm"));
        startTimeEdit->setDisplayFormat("hh:mm"); // Убедиться в единообразном формате
        layout->addWidget(new QLabel("Время начала (hh:mm):"));
        layout->addWidget(startTimeEdit);

        endTimeEdit = new QTimeEdit(QTime::fromString(QString::fromStdString(end_time), "hh:mm"));
        endTimeEdit->setDisplayFormat("hh:mm"); // Убедиться в единообразном формате
        layout->addWidget(new QLabel("Время окончания (hh:mm):"));
        layout->addWidget(endTimeEdit);

        intervalEdit = new QLineEdit(QString::number(capture_interval));
        intervalEdit->setValidator(new QIntValidator(1, 86400, this)); // Интервал от 1 сек до 24 часов
        layout->addWidget(new QLabel("Интервал съемки (с):"));
        layout->addWidget(intervalEdit);

        latEdit = new QLineEdit(QString::number(0.0));
        // latEdit->setValidator(new QDoubleValidator(-90.0, 90.0, 8, this)); // Валидатор широты-
        QDoubleValidator *validatorlat = new QDoubleValidator(-90.0, 90.0, 8, this);
        validatorlat->setLocale(QLocale::c()); // Устанавливаем локаль
        latEdit->setValidator(validatorlat);
        layout->addWidget(new QLabel("Широта центра:"));
        layout->addWidget(latEdit);

        lonEdit = new QLineEdit(QString::number(0.0));
      //  lonEdit->setValidator(new QDoubleValidator(-180.0, 180.0, 8, this)); // Валидатор долготы
       QDoubleValidator *validatorlon = new QDoubleValidator(-180.0, 180.0, 8, this);
        validatorlon->setLocale(QLocale::c()); // Устанавливаем локаль
        lonEdit->setValidator(validatorlon);
        layout->addWidget(new QLabel("Долгота центра:"));
        layout->addWidget(lonEdit);

        radiusEdit = new QLineEdit(QString::number(radius));
        radiusEdit->setValidator(new QIntValidator(0, 1000, this)); // Валидатор радиуса (например, до 1000 км)
        layout->addWidget(new QLabel("Радиус съемки (км):"));
        layout->addWidget(radiusEdit);

        // Удалено: Поле для количества точек countRowEdit

        // Поле для указания пути сохранения окончательных снимков
        dirEdit = new QLineEdit(QString::fromStdString(screenshot_directory));
        layout->addWidget(new QLabel("Путь для сохранения окончательных снимков:"));
        layout->addWidget(dirEdit);

        // Кнопка для выбора пути
        QPushButton *browseButton = new QPushButton("Обзор...");
        layout->addWidget(browseButton);
        connect(browseButton, &QPushButton::clicked, this, &SnapshotApp::browseDirectory);

        // Кнопка для старта
        QPushButton *startButton = new QPushButton("Начать съемку");
        layout->addWidget(startButton);
        connect(startButton, &QPushButton::clicked, this, &SnapshotApp::startCapture);

        // Кнопка для остановки
        QPushButton *stopButton = new QPushButton("Остановить съемку");
        layout->addWidget(stopButton);
        connect(stopButton, &QPushButton::clicked, this, &SnapshotApp::stopCapture);

        // CaptureThread создается при запуске захвата
        captureThread = nullptr;

        // Инициализируем библиотеку CURL
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    ~SnapshotApp()
    {
        // Убедиться, что поток остановлен и удален при выходе из приложения
        if (captureThread)
        {
            if (captureThread->isRunning())
            {
                captureThread->stop();     // Сигнализировать потоку остановиться
                captureThread->wait(5000); // Ждать до 5 секунд завершения потока
                if (captureThread->isRunning())
                {
                    std::cerr << "Предупреждение: Поток захвата не завершился корректно." << std::endl;
                    // Можно рассмотреть возможность принудительного завершения, если wait не удался, но обычно wait предпочтительнее.
                }
            }
            // deleteLater() подключен к finished(), но явное удаление также безопасно здесь, если поток не запущен.
            // Если он запущен и wait не удался, deleteLater может быть рискованным.
            // Соединение сигнала finished() обычно достаточно для очистки, когда поток завершается.
        }

        // Очищаем библиотеку CURL
        curl_global_cleanup();

        // Очистить временный каталог при выходе из приложения
        // Поток очищает его при выходе, но это подстраховка.
        std::string final_temp_dir_path = screenshot_directory + "/" + screen_temp_directory_name;
        std::error_code ec;
        std::filesystem::remove_all(final_temp_dir_path, ec);
        if (ec)
        {
            std::cerr << "Ошибка при окончательной очистке временного каталога '" << final_temp_dir_path << "' при выходе приложения с помощью filesystem: " << ec.message() << std::endl;
        }
        else
        {
            // std::cout << "Окончательный временный каталог очищен при выходе приложения: " << final_temp_dir_path << std::endl; // Слишком много сообщений при выходе
        }
    }

public slots:
    void startCapture()
    {
        // Проверить, запущен ли уже поток
        if (captureThread && captureThread->isRunning())
        {
            QMessageBox::warning(this, "Предупреждение", "Захват уже запущен. Остановите его, прежде чем начать новый.");
            return;
        }

        // Получить данные из GUI
        start_time = startTimeEdit->time().toString("hh:mm").toStdString();
        end_time = endTimeEdit->time().toString("hh:mm").toStdString();

        bool ok_interval, ok_radius, ok_lat, ok_lon;
        capture_interval = intervalEdit->text().toInt(&ok_interval);
        radius = radiusEdit->text().toInt(&ok_radius);
        double center_lat = latEdit->text().toDouble(&ok_lat);
        double center_lon = lonEdit->text().toDouble(&ok_lon);

        // Проверить введенные значения
        if (!ok_interval || capture_interval <= 0)
        {
            QMessageBox::critical(this, "Ошибка", "Неверное значение интервала съемки.");
            return;
        }
        if (!ok_radius || radius < 0)
        {
            QMessageBox::critical(this, "Ошибка", "Неверное значение радиуса съемки.");
            return;
        }
        if (!ok_lat || center_lat < -90.0 || center_lat > 90.0)
        {
            QMessageBox::critical(this, "Ошибка", "Неверное значение широты центра.");
            return;
        }
        if (!ok_lon || center_lon < -180.0 || center_lon > 180.0)
        {
            QMessageBox::critical(this, "Ошибка", "Неверное значение долготы центра.");
            return;
        }

        screenshot_directory = dirEdit->text().toStdString();

        // Проверить, существует ли конечный выходной каталог
        if (!std::filesystem::exists(screenshot_directory))
        {
            QMessageBox::critical(this, "Ошибка", "Указанный каталог для сохранения окончательных снимков не существует.");
            return;
        }

        // Установить центральные координаты
        std::pair<double, double> current_coordinat_centr = {center_lat, center_lon};

        // Очистить предыдущий список координат
        coordinat.clear();

        // --- Генерация координат сетки на основе центра и радиуса (1 км примерно 0.01 градуса на экваторе) ---
        double lat0 = current_coordinat_centr.first;
        double lon0 = current_coordinat_centr.second;
        double r_km = static_cast<double>(radius); // Радиус в километрах

        // Правило: 1 км примерно равен 0.01 градуса широты.
        // Градусы долготы зависят от широты (cos(lat)).
        // Используется фиксированный шаг 0.02 градуса между точками сетки.
        const double deg_per_km_at_equator = 0.01; // Примерно градусов на км для широты/долготы на экваторе
        const double step_deg_x = 0.0193;          // Фиксированный шаг сетки в градусах
        const double step_deg_y = 0.0137;

        // Вычислить общий диапазон в градусах, необходимый для покрытия радиуса R с шагом step_deg
        // Общий диапазон - от центра - R_deg до центра + R_deg
        double span_from_center_lat_deg = r_km * deg_per_km_at_equator;
        // Для долготы скорректировать эквивалент градуса на основе широты
        double cos_lat0 = std::cos(lat0 * M_PI / 180.0);
        // Избегать деления на ноль или близкое к нулю на полюсах
        if (std::abs(cos_lat0) < std::numeric_limits<double>::epsilon())
        {
            cos_lat0 = std::numeric_limits<double>::epsilon(); // Использовать очень маленькое число вместо 0
        }
        double span_from_center_lon_deg = r_km * deg_per_km_at_equator / cos_lat0;

        // Вычислить количество шагов (интервалов step_deg) от центра до края
        // Это определяет, сколько точек нужно на *одной стороне* от центра, включая центр.
        int num_steps_from_center_lat = static_cast<int>(std::floor(span_from_center_lat_deg / step_deg_y));
        int num_steps_from_center_lon = static_cast<int>(std::floor(span_from_center_lon_deg / step_deg_x));

        // Общее количество точек вдоль каждой оси (включая центральную точку)
        // N = (шаги влево) + 1 (центр) + (шаги вправо)
        // Поскольку сетка N x N, N должно быть одинаковым для обоих измерений.
        // Берем максимальное количество шагов, чтобы убедиться, что радиус покрыт в обоих направлениях.
        // Количество точек вдоль одной оси будет 2 * max_steps + 1.
        int N = std::max(1, 2 * std::max(num_steps_from_center_lat, num_steps_from_center_lon) + 1);

        // Вычислить начальную координату сетки (левый нижний угол)
        // Сдвиг от центра на (N-1)/2 шагов влево и вниз.
        double start_lat = lat0 - (N - 1) / 2.0 * step_deg_y - step_deg_y / 2;
        double start_lon = lon0 - (N - 1) / 2.0 * step_deg_x - step_deg_x / 2;

        // Генерация точек сетки
        for (int yy = 0; yy < N; ++yy)
        {
            for (int xx = 0; xx < N; ++xx)
            {
                double current_lat = start_lat + yy * step_deg_y;
                double current_lon = start_lon + xx * step_deg_x;
                coordinat.push_back({current_lat, current_lon});
            }
        }
        // --- Конец генерации координат ---

        // std::cout << "Генерация сетки: Радиус=" << radius << " км, Шаг=" << step_deg << " град. => Сетка " << N << "x" << N << " = " << coordinat.size() << " координат." << std::endl;

        // Создать новый экземпляр потока
        captureThread = new CaptureThread();
        // Подключить сигнал finished к deleteLater для очистки объекта потока
        connect(captureThread, &QThread::finished, captureThread, &QObject::deleteLater);

        // Передать конечный выходной каталог потоку
        captureThread->setDirectory(screenshot_directory);

        // Запустить поток
        captureThread->start();
        // QMessageBox::information(this, "Информация", "Съемка начата."); // Может быть назойливым
    }

    void stopCapture()
    {
        if (captureThread && captureThread->isRunning())
        {
            std::cout << "Нажата кнопка 'Остановить'. Сигнализируем потоку остановиться." << std::endl;
            captureThread->stop(); // Сигнализировать потоку остановиться
            // НЕ вызывать wait() здесь, так как это заблокирует поток GUI
        }
        else
        {
            QMessageBox::information(this, "Информация", "Захват не запущен.");
        }
    }

    void browseDirectory()
    {
        QString directory = QFileDialog::getExistingDirectory(this, "Выбор каталога для сохранения снимков", QString::fromStdString(screenshot_directory));
        if (!directory.isEmpty())
        {
            dirEdit->setText(directory);
        }
    }

    void onSnapshotDone(const QString &filename)
    {
        // Этот слот будет подключен, если вам нужны обновления GUI для каждого отдельного снимка.
        // При объединении этот сигнал менее важен.
        // std::cout << "Получен сигнал: Снимок сохранен: " << filename.toStdString() << std::endl;
    }

private:
    QTimeEdit *startTimeEdit;
    QTimeEdit *endTimeEdit;
    QLineEdit *intervalEdit;
    QLineEdit *latEdit;
    QLineEdit *lonEdit;
    QLineEdit *radiusEdit;
    QLineEdit *dirEdit;
    CaptureThread *captureThread; // Указатель на рабочий поток
    // coordinat является глобальной переменной
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    std::locale::global(std::locale("C"));
    // Создать каталог по умолчанию для окончательных скриншотов, если он не существует
    std::error_code ec;
    if (!std::filesystem::exists(screenshot_directory))
    {
        std::filesystem::create_directory(screenshot_directory, ec);
        if (ec)
        {
            std::cerr << "Ошибка создания каталога скриншотов по умолчанию '" << screenshot_directory << "': " << ec.message() << std::endl;
            // Решить, является ли это фатальной ошибкой или пользователь может выбрать другой каталог.
            // Пока что позволим приложению запуститься, проверка каталога выполняется перед захватом.
        }
    }

    // Временный каталог управляется потоком (создается и очищается внутри run)
    // и потенциально деструктором приложения для безопасности.

    SnapshotApp window;
    window.resize(400, 450); // Скорректированный размер окна
    window.show();

    return app.exec();
}

#include "main.moc"
