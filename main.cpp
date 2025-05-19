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
#include <cmath>    // Для std::cos, std::floor, M_PI
#include <limits>   // Для std::numeric_limits
#include <cstdio>   // Для snprintf
#include <algorithm> // Для std::max

// Глобальные переменные с параметрами
std::vector<std::pair<double, double>> coordinat; // Координаты для скриншотов
// coordinat_centr будет считываться из GUI
int capture_interval = 3600;                   // Интервал в секундах
std::string start_time = "07:00";
std::string end_time = "23:00";
int radius = 5; // Радиус в км (значение по умолчанию)

// Путь по умолчанию для сохранения скриншотов
std::string screenshot_directory = "./screenshots";

// Поток для выполнения захвата скриншотов
class CaptureThread : public QThread {
    Q_OBJECT

public:
    CaptureThread(QObject* parent = nullptr) : QThread(parent), running(true) {}

    void setDirectory(const std::string& directory) { saving_directory = directory; }

    // Метод для безопасной остановки потока
    void stop() {
        running = false;
    }

    void run() override {
        running = true; // Сбрасываем флаг при старте
        while (running) { // Проверяем флаг в условии цикла
            std::time_t now = std::time(nullptr);
            std::tm* current_time = std::localtime(&now);

            // Проверка по времени
            if (current_time->tm_hour < std::stoi(start_time.substr(0, 2)) ||
                current_time->tm_hour >= std::stoi(end_time.substr(0, 2))) {
                // Ждем до следующей проверки времени, но не блокируем возможность остановки
                auto start_sleep = std::chrono::high_resolution_clock::now();
                while (running && std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - start_sleep).count() < 60) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Короткие интервалы сна
                }
                continue;
            }

            // Генерация скриншотов
            // Доступ к глобальному вектору 'coordinat' должен быть синхронизирован
            // в многопоточном приложении, если он может изменяться во время выполнения run().
            // Здесь предполагается, что он заполняется только перед стартом потока.
            if (coordinat.empty()) {
                std::cerr << "Список координат пуст. Захват невозможен." << std::endl;
                // Ждем интервал захвата, чтобы не зацикливаться, но с возможностью остановки
                auto start_sleep = std::chrono::high_resolution_clock::now();
                while (running && std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - start_sleep).count() < capture_interval) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                continue;
            }

            for (size_t u = 0; u < coordinat.size(); ++u) {
                if (!running)
                    break; // Проверяем флаг остановки перед каждым снимком
                createSnapshot(coordinat[u], saving_directory, "Скриншот_%Y-%m-%d_%H-%M-%S", static_cast<int>(u), current_time);
            }

            // Ждем до следующего захвата, также проверяя флаг остановки
            auto start_sleep = std::chrono::high_resolution_clock::now();
            while (running && std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - start_sleep).count() < capture_interval) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Короткие интервалы сна
            }
        }
        std::cout << "Capture thread stopped." << std::endl;
    }

signals:
    void snapshotDone(const QString& filename); // Сигнал при успешном сохранении

private:
    std::string saving_directory;
    bool running; // Флаг для управления выполнением потока

    void createSnapshot(std::pair<double, double> url, const std::string& directory, const std::string& format, int index, std::tm* current_time) {
        char buffer[80];
        std::strftime(buffer, sizeof(buffer), format.c_str(), current_time);
        std::string file_name = directory + "/" + buffer + "_" + std::to_string(index) + ".png";

        // Проверяем существование папки
        if (!std::filesystem::exists(directory)) {
            std::cerr << "Каталог не существует: " << directory << std::endl;
            return;
        }

        // Формируем URL запроса к API Yandex Static Maps
        // Порядок координат: долгота, широта (lon,lat)
        // Используем snprintf для форматирования double с точкой в качестве разделителя
        char lat_str[32]; // Буфер для строки широты
        char lon_str[32]; // Буфер для строки долготы

        // Форматируем double с фиксированной точностью (например, 8 знаков после запятой)
        // snprintf в стандартной C локали всегда использует точку для десятичного разделителя
        snprintf(lat_str, sizeof(lat_str), "%.8f", url.first);
        snprintf(lon_str, sizeof(lon_str), "%.8f", url.second);

        // Используем spn для указания области. spn=lon,lat в градусах.
        // Ширина и высота охватываемой области карты (в градусах).
        // Можно связать spn с радиусом и размером изображения, но проще использовать фиксированный масштаб z.
        // z=13 соответствует области примерно 1км x 0.7км на средних широтах.
        // Если мы генерируем точки сетки с шагом 0.02 градуса (~2.2 км), то z=13 слишком крупный масштаб для одной точки.
        // Давайте попробуем z=10 (~10 км) или z=11 (~5 км), или z=12 (~2.5 км) чтобы видеть окружающие точки.
        // Выберем z=11 как компромисс. Или можно попробовать использовать spn, например spn=0.02,0.02
        // чтобы каждая картинка показывала квадрат 0.02x0.02 градуса.

         std::locale::global(std::locale("C"));

         std::string api_url = "https://static-maps.yandex.ru/1.x/?ll=" +
                              std::to_string(url.second) + "," + std::to_string(url.first) +
                              "&spn=0.01,0.01&size=450,450&l=map,trf";

        // Если хотите, чтобы каждый снимок охватывал площадь 0.02x0.02 градуса:
        // std::string api_url = "https://static-maps.yandex.ru/1.x/?ll=" +
        //                       std::string(lon_str) + "," + std::string(lat_str) +
        //                       "&size=650,450&spn=0.02,0.02&l=map,trf";


        // std::cout << "Request URL: " << api_url << std::endl; // Для отладки

        CURL *curl = curl_easy_init();
        if (curl) {
            FILE *file = fopen(file_name.c_str(), "wb");
            if (file) {
                curl_easy_setopt(curl, CURLOPT_URL, api_url.c_str());
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
                curl_easy_setopt(curl, CURLOPT_HEADER, 0L);
                curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L); // Добавляем проверку ошибок HTTP
                curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L); // Можно включить для подробного вывода CURL

                CURLcode res = curl_easy_perform(curl);
                long http_code = 0;
                curl_easy_getinfo(curl, CURLINFO_HTTP_CODE, &http_code); // Получаем HTTP статус

                fclose(file);

                if (res == CURLE_OK && http_code >= 200 && http_code < 300) {
                    std::ifstream ifs(file_name, std::ios::binary | std::ios::ate);
                    if (ifs.is_open() && ifs.tellg() > 0) { // Проверяем, что файл открыт и не пустой
                        std::cout << "Скриншот сохранен: " << file_name << std::endl;
                        // emit snapshotDone(QString::fromStdString(file_name)); // Генерируем сигнал
                    } else {
                        std::cerr << "Ошибка: пустой или не открытый файл -> " << file_name << std::endl;
                        std::remove(file_name.c_str()); // Удаляем пустой файл
                    }
                } else {
                    std::cerr << "CURL Ошибка (код: " << res << ", HTTP: " << http_code << ") при загрузке " << api_url << ": " << curl_easy_strerror(res) << std::endl;
                    std::remove(file_name.c_str()); // Удаляем файл, если была ошибка CURL или HTTP
                }
            } else {
                std::cerr << "Ошибка при открытии файла для записи: " << file_name << std::endl;
            }
            curl_easy_cleanup(curl);
        } else {
            std::cerr << "Ошибка инициализации CURL." << std::endl;
        }
    }
};

// Основное окно приложения
class SnapshotApp : public QWidget {
    Q_OBJECT

public:
    SnapshotApp(QWidget* parent = nullptr) : QWidget(parent) {
        setWindowTitle("Снимки карты");

        QVBoxLayout* layout = new QVBoxLayout(this);

        // Установка времени
        startTimeEdit = new QTimeEdit(QTime::fromString(QString::fromStdString(start_time), "hh:mm"));
        layout->addWidget(new QLabel("Время начала (hh:mm):"));
        layout->addWidget(startTimeEdit);

        endTimeEdit = new QTimeEdit(QTime::fromString(QString::fromStdString(end_time), "hh:mm"));
        layout->addWidget(new QLabel("Время окончания (hh:mm):"));
        layout->addWidget(endTimeEdit);

        intervalEdit = new QLineEdit(QString::number(capture_interval));
        layout->addWidget(new QLabel("Интервал съемки (с):"));
        layout->addWidget(intervalEdit);

        latEdit = new QLineEdit(QString::number(0));
        layout->addWidget(new QLabel("Широта центра:"));
        layout->addWidget(latEdit);

        lonEdit = new QLineEdit(QString::number(0));
        layout->addWidget(new QLabel("Долгота центра:"));
        layout->addWidget(lonEdit);

        radiusEdit = new QLineEdit(QString::number(radius));
        layout->addWidget(new QLabel("Радиус съемки (км):"));
        layout->addWidget(radiusEdit);

        // Удалено: Поле для количества точек countRowEdit

        // Поле для указания пути
        dirEdit = new QLineEdit(QString::fromStdString(screenshot_directory));
        layout->addWidget(new QLabel("Путь для сохранения:"));
        layout->addWidget(dirEdit);

        // Кнопка для выбора пути
        QPushButton* browseButton = new QPushButton("Обзор...");
        layout->addWidget(browseButton);
        connect(browseButton, &QPushButton::clicked, this, &SnapshotApp::browseDirectory);

        // Кнопка для старта
        QPushButton* startButton = new QPushButton("Начать съемку");
        layout->addWidget(startButton);
        connect(startButton, &QPushButton::clicked, this, &SnapshotApp::startCapture);

        // Кнопка для остановки
        QPushButton* stopButton = new QPushButton("Остановить съемку");
        layout->addWidget(stopButton);
        connect(stopButton, &QPushButton::clicked, this, &SnapshotApp::stopCapture);

        captureThread = new CaptureThread();
        connect(captureThread, &CaptureThread::snapshotDone, this, &SnapshotApp::onSnapshotDone);
        // Соединяем сигнал finished потока со слотом очистки
        connect(captureThread, &QThread::finished, captureThread, &QObject::deleteLater);

        // Инициализируем библиотеку CURL
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    ~SnapshotApp() {
        // Очищаем библиотеку CURL
        curl_global_cleanup();
        // Убедимся, что поток остановлен и удален
        if (captureThread) {
            if (captureThread->isRunning()) {
                captureThread->stop(); // Сигнализируем потоку об остановке
                captureThread->wait(); // Ждем завершения потока
            }
            // captureThread->deleteLater(); // Уже соединено сигналом finished
        }
    }

public slots:
    void startCapture() {
        // Проверяем, не запущен ли поток уже
        if (captureThread && captureThread->isRunning()) {
            QMessageBox::warning(this, "Предупреждение", "Захват уже запущен. Остановите его, прежде чем начать новый.");
            return;
        }

        // Получаем данные из GUI
        start_time = startTimeEdit->time().toString("hh:mm").toStdString();
        end_time = endTimeEdit->time().toString("hh:mm").toStdString();
        capture_interval = intervalEdit->text().toInt();
        radius = radiusEdit->text().toInt(); // Читаем значение радиуса

        screenshot_directory = dirEdit->text().toStdString();

        // Проверяем существование каталога
        if (!std::filesystem::exists(screenshot_directory)) {
            QMessageBox::critical(this, "Ошибка", "Указанный каталог не существует.");
            return;
        }

        // Получаем центральные координаты
        std::pair<double, double> current_coordinat_centr; // Локальная переменная для текущего запуска
        current_coordinat_centr.first = latEdit->text().toDouble();
        current_coordinat_centr.second = lonEdit->text().toDouble();

        // Очищаем предыдущий список координат для съемки
        coordinat.clear();

        // --- Генерируем координаты сетки на основе центра и радиуса (1 км = 0.01 градуса) ---
        double lat0 = current_coordinat_centr.first;
        double lon0 = current_coordinat_centr.second;
        double r_km = static_cast<double>(radius); // Радиус в километрах

        // Правило: 1 км = 0.01 градуса
        const double km_to_deg = 0.01;
        const double step_deg = 0.02; // Фиксированный шаг сетки в градусах (как в старом коде)

        // Общий охват в градусах, соответствующий радиусу R
        // Это расстояние от центра до края области по каждому направлению.
        double span_from_center_deg = r_km * km_to_deg;

        // Количество шагов (интервалов 0.02 градуса) от центра до края
        int num_steps_from_center = static_cast<int>(std::floor(span_from_center_deg / step_deg));

        // Общее количество точек вдоль одной оси (включая центральную)
        // 2 * num_steps_from_center для обеих сторон от центра + 1 для центральной точки
        int N = std::max(1, 2 * num_steps_from_center + 1);

        // Начальная координата сетки (нижний левый угол)
        // Сдвигаемся от центра на (N-1)/2 шагов влево и вниз
        double start_lat = lat0 - (N - 1) / 2.0 * step_deg;
        double start_lon = lon0 - (N - 1) / 2.0 * step_deg;

        // Генерация точек сетки
        for (int yy = 0; yy < N; ++yy) {
            for (int xx = 0; xx < N; ++xx) {
                double current_lat = start_lat + yy * step_deg;
                double current_lon = start_lon + xx * step_deg;
                coordinat.push_back({current_lat, current_lon});
            }
        }
        // --- Конец генерации координат ---


        std::cout << "Сгенерировано " << coordinat.size() << " координат для съемки." << std::endl;


        // Передаем каталог потоку
        captureThread->setDirectory(screenshot_directory);

        // Запускаем поток
        captureThread->start();
        // QMessageBox::information(this, "Информация", "Съемка начата."); // Лучше не показывать для каждого запуска
    }

    void stopCapture() {
        if (captureThread && captureThread->isRunning()) {
            captureThread->stop(); // Сигнализируем потоку об остановке
            // Не вызываем wait() здесь, чтобы не блокировать основной поток GUI
        } else {
            QMessageBox::information(this, "Информация", "Захват не запущен.");
        }
    }

    void browseDirectory() {
        QString directory = QFileDialog::getExistingDirectory(this, "Выбор каталога", QString::fromStdString(screenshot_directory));
        if (!directory.isEmpty()) {
            dirEdit->setText(directory);
        }
    }

    void onSnapshotDone(const QString& filename) {
        // Можно вывести сообщение о каждом сохраненном снимке
        // std::cout << "Сигнал получен: Снимок сохранен: " << filename.toStdString() << std::endl;
        // Или обновить что-то в GUI
    }

private:
    QTimeEdit* startTimeEdit;
    QTimeEdit* endTimeEdit;
    QLineEdit* intervalEdit;
    QLineEdit* latEdit;
    QLineEdit* lonEdit;
    QLineEdit* radiusEdit;
    // Удалено: QLineEdit* countRowEdit;
    QLineEdit* dirEdit;
    CaptureThread* captureThread;
    // coordinat является глобальным
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // Удаляем устаревший код генерации координат из main (он перенесен в startCapture)

    // Создание стандартной папки, если ее нет
    if (!std::filesystem::exists(screenshot_directory)) {
        std::filesystem::create_directory(screenshot_directory);
    }

    SnapshotApp window;
    window.resize(400, 450); // Уменьшен размер окна, т.к. убрано одно поле
    window.show();

    return app.exec();
}

#include "main.moc"
