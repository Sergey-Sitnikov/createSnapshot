#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QTimeEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QTimer>
#include <QDateTime>
#include <QDir>
#include <QScrollArea>

#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <ctime>
#include <fstream>
#include <curl/curl.h>
#include <filesystem>
#include <vector>
#include <QThread>
#include <QObject>
#include <functional> // Добавьте этот заголовочный файл

// Параметры по умолчанию
std::vector<std::pair<double, double>> urls = {{38.989986, 45.044545}, {39.007159, 45.122231}};
int capture_interval = 3600; // Интервал захвата в секундах
std::string start_time_str = "07:00";
std::string end_time_str = "23:00";
std::string save_path = "./screenshots";
double latitude = 38.989986;
double longitude = 45.044545;
double distance = 0.01;
bool is_running = false;

class CaptureThread : public QThread {
    Q_OBJECT
public:
    CaptureThread(std::pair<double, double> url, std::string address, std::string format, int i, tm *current_time)
        : url_(url), address_(address), format_(format), i_(i), current_time_(current_time) {}

signals:
    void finished();

protected:
    void run() override {
        char buffer[80];
        std::strftime(buffer, sizeof(buffer), format_.c_str(), current_time_);
        std::string file_name = address_ + "/" + buffer + '_' + std::to_string(i_) + ".png";

        if (!std::filesystem::exists(address_)) {
            std::cerr << "Каталог для сохранения не существует: " << address_ << std::endl;
            emit finished();
            return;
        }

        std::string api_url = "https://static-maps.yandex.ru/1.x/?ll=" +
                              std::to_string(url_.first) + "," + std::to_string(url_.second) +
                              "&size=450,450&spn=" + std::to_string(distance) + "," + std::to_string(distance) + "&l=map,trf";

        CURL *curl = curl_easy_init();
        if (curl) {
            FILE *file = fopen(file_name.c_str(), "wb");
            if (file) {
                curl_easy_setopt(curl, CURLOPT_URL, api_url.c_str());
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
                curl_easy_setopt(curl, CURLOPT_HEADER, 0L);
                curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
                curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L); // Уменьшено verbose

                CURLcode res = curl_easy_perform(curl);
                fclose(file);

                if (res != CURLE_OK) {
                    std::cerr << "Ошибка при выполнении запроса: " << curl_easy_strerror(res) << std::endl;
                    std::remove(file_name.c_str());
                } else {
                    std::ifstream ifs(file_name, std::ios::binary | std::ios::ate);
                    std::streamsize size = ifs.tellg();
                    ifs.close();

                    if (size == 0) {
                        std::cerr << "Ошибка: файл " << file_name << " пустой." << std::endl;
                        std::remove(file_name.c_str());
                    } else {
                        std::cout << "Скриншот сохранен: " << file_name << std::endl;
                    }
                }
            } else {
                std::cerr << "Ошибка при открытии файла: " << file_name << std::endl;
            }
            curl_easy_cleanup(curl);
        }
        emit finished();
    }

private:
    std::pair<double, double> url_;
    std::string address_;
    std::string format_;
    int i_;
    tm *current_time_;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // Главный виджет
    QWidget *window = new QWidget;
    window->setWindowTitle("Geo Screenshot Capture");

    // Layout
    QVBoxLayout *mainLayout = new QVBoxLayout;

    // Форма для ввода параметров
    QTimeEdit *startTimeEdit = new QTimeEdit(QTime::fromString(QString::fromStdString(start_time_str), "hh:mm"));
    QTimeEdit *endTimeEdit = new QTimeEdit(QTime::fromString(QString::fromStdString(end_time_str), "hh:mm"));
    QLineEdit *savePathEdit = new QLineEdit(QString::fromStdString(save_path));
    QLineEdit *distanceEdit = new QLineEdit(QString::number(distance));

    // Кнопка для выбора пути сохранения
    QPushButton *browseButton = new QPushButton("Обзор");
    QObject::connect(browseButton, &QPushButton::clicked, [&]() {
        QString dir = QFileDialog::getExistingDirectory(window, "Выберите папку для сохранения",
                                                         QString::fromStdString(save_path),
                                                         QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        if (!dir.isEmpty()) {
            savePathEdit->setText(dir);
        }
    });

    // Кнопка запуска/остановки
    QPushButton *startStopButton = new QPushButton("Запустить");

    // Layout для выбора пути
    QHBoxLayout *pathLayout = new QHBoxLayout;
    pathLayout->addWidget(savePathEdit);
    pathLayout->addWidget(browseButton);

    // Layout для координат
    QVBoxLayout *coordsLayout = new QVBoxLayout;

    //  Динамический список координат
    QWidget *coordsWidget = new QWidget;
    QVBoxLayout *coordsListLayout = new QVBoxLayout(coordsWidget);

    // Функция для добавления новой строки координат
    std::function<void(QWidget*, QWidget*)> addCoordRow;

    addCoordRow = [&](QWidget* latEditWidget, QWidget* lonEditWidget) {
        QWidget *coordRow = new QWidget;
        QHBoxLayout *rowLayout = new QHBoxLayout(coordRow);

        QLineEdit *latEdit = (latEditWidget) ? qobject_cast<QLineEdit*>(latEditWidget) : new QLineEdit();
        QLineEdit *lonEdit = (lonEditWidget) ? qobject_cast<QLineEdit*>(lonEditWidget) : new QLineEdit();

        rowLayout->addWidget(new QLabel("Широта:"));
        rowLayout->addWidget(latEdit);
        rowLayout->addWidget(new QLabel("Долгота:"));
        rowLayout->addWidget(lonEdit);

        coordsListLayout->addWidget(coordRow);

        //  Автоматическое добавление новой строки при заполнении текущей
        QObject::connect(latEdit, &QLineEdit::textChanged, [=](const QString &text) {
            if (!text.isEmpty() && !lonEdit->text().isEmpty()) {
                addCoordRow(nullptr, latEdit->parentWidget());
            }
        });

        QObject::connect(lonEdit, &QLineEdit::textChanged, [=](const QString &text) {
            if (!text.isEmpty() && !latEdit->text().isEmpty()) {
                addCoordRow(lonEdit->parentWidget(), nullptr);
            }
        });
    };
    //  Кнопка для добавления координат принудительно
    QPushButton *addCoordButton = new QPushButton("Добавить координаты");
    QObject::connect(addCoordButton, &QPushButton::clicked, [&]() {
        addCoordRow(nullptr, nullptr);
    });

    // Добавляем первую строку координат
    addCoordRow(nullptr, nullptr);

    // Scroll Area для координат
    QScrollArea *scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    scrollArea->setWidget(coordsWidget);

    coordsLayout->addWidget(scrollArea);
    coordsLayout->addWidget(addCoordButton);

    // Добавление элементов в главный layout
    mainLayout->addWidget(new QLabel("Время начала (hh:mm):"));
    mainLayout->addWidget(startTimeEdit);
    mainLayout->addWidget(new QLabel("Время окончания (hh:mm):"));
    mainLayout->addWidget(endTimeEdit);
    mainLayout->addWidget(new QLabel("Путь для сохранения:"));
    mainLayout->addLayout(pathLayout);
    mainLayout->addWidget(new QLabel("Расстояние (spn):"));
    mainLayout->addWidget(distanceEdit);
    mainLayout->addLayout(coordsLayout);
    mainLayout->addWidget(startStopButton);

    // Установка главного layout для окна
    window->setLayout(mainLayout);

    // Таймер для периодического создания скриншотов
    QTimer *timer = new QTimer();

    // Функция для создания скриншотов
    auto captureFunction = [&]() {
        if (!is_running) return;

        std::cout << "captureFunction called" << std::endl; // Добавьте это

        QTime current_time = QTime::currentTime();
        QTime start_time = startTimeEdit->time();
        QTime end_time = endTimeEdit->time();

        if ((current_time >= start_time) && (current_time <= end_time)) {
            std::cout << "Time condition met" << std::endl; // Добавьте это
            std::time_t now = std::time(nullptr);
            std::tm *current_tm = std::localtime(&now);

            //  Получаем координаты из списка
            urls.clear();
            for (int i = 0; i < coordsListLayout->count(); ++i) {
                QWidget *coordRow = coordsListLayout->itemAt(i)->widget();
                QHBoxLayout *rowLayout = qobject_cast<QHBoxLayout*>(coordRow->layout());
                if (rowLayout) {
                    QLineEdit *latEdit = qobject_cast<QLineEdit*>(rowLayout->itemAt(1)->widget());
                    QLineEdit *lonEdit = qobject_cast<QLineEdit*>(rowLayout->itemAt(3)->widget());
                    if (latEdit && lonEdit) {
                        bool latOk, lonOk;
                        double lat = latEdit->text().toDouble(&latOk);
                        double lon = lonEdit->text().toDouble(&lonOk);
                        if (latOk && lonOk) {
                            urls.push_back({lat, lon});
                        }
                    }
                }
            }

            for (size_t u = 0; u < urls.size(); ++u) {
                std::cout << "Creating CaptureThread for URL " << u << std::endl; // Добавьте это
                CaptureThread *captureThread = new CaptureThread(urls[u], save_path, "Скрин %Y-%m-%d в %H-%M", static_cast<int>(u), current_tm);
                QObject::connect(captureThread, &CaptureThread::finished, captureThread, &QObject::deleteLater);
                captureThread->start();
            }
        } else {
            std::cout << "Time condition not met" << std::endl; // Добавьте это
        }
    };

    // Соединение сигнала таймера со слотом captureFunction
    QObject::connect(timer, &QTimer::timeout, captureFunction);

    // Обработчик нажатия кнопки "Запустить/Остановить"
    QObject::connect(startStopButton, &QPushButton::clicked, [&]() {
        if (!is_running) {
            // Считывание данных из полей формы
            QTime start_time = startTimeEdit->time();
            QTime end_time = endTimeEdit->time();
            save_path = savePathEdit->text().toStdString();

            bool ok;
            distance = distanceEdit->text().toDouble(&ok);
            if (!ok) {
                QMessageBox::warning(window, "Ошибка", "Неверный формат расстояния.");
                return;
            }

            // Добавьте это
            std::cout << "Start Time: " << start_time.toString().toStdString() << std::endl;
            std::cout << "End Time: " << end_time.toString().toStdString() << std::endl;
            std::cout << "Save Path: " << save_path << std::endl;
            std::cout << "Distance: " << distance << std::endl;

            // Проверка пути для сохранения
            if (!QDir(QString::fromStdString(save_path)).exists()) {
                QMessageBox::warning(window, "Ошибка", "Указанный путь для сохранения не существует.");
                return;
            }

            // Запуск таймера
            is_running = true;
            startStopButton->setText("Остановить");
            timer->start(capture_interval * 1000); // Интервал в миллисекундах
            std::cout << "Захват скриншотов запущен..." << std::endl;
        } else {
            // Остановка таймера
            is_running = false;
            startStopButton->setText("Запустить");
            timer->stop();
            std::cout << "Захват скриншотов остановлен." << std::endl;
        }
    });

    // Запуск GUI

    window->show();
    return app.exec();
}ауф

#include "main.moc"
