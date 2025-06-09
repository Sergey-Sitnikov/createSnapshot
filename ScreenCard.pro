TEMPLATE = app
CONFIG += qt warn_on release static
QT += core gui widgets network
DEFINES += CURL_STATICLIB
# Путь к каталогу библиотек MXE
LIBS += -L/home/ssv/mxe/usr/x86_64-w64-mingw32.static/lib
# Основные используемые библиотеки и их зависимости
LIBS += -lcurl \
        -lssh2 \
        -lnghttp2 \
        -lidn2 \
        -lpsl \
        -lunistring \ # <--- ДОБАВЛЕНО: для libidn2 и libpsl
        -lssl \
        -lcrypto \
        -lbcrypt \    # <--- ДОБАВЛЕНО: для BCrypt функций, используемых libcurl/libssh2
        -lzstd \
        -lbrotlidec \
        -lbrotlicommon \
        -lwldap32
# Системные библиотеки Windows и другие зависимости
# (Это ваш предыдущий список, из которого удалены дубликаты и zstd, который уже выше)
LIBS += -lwinspool \
        -lwtsapi32 \
        -lfontconfig \
        -lexpat \
        -ldwrite \
        -ld2d1 \
        -luxtheme \
        -ldwmapi \
        -ld3d11 \
        -ldxgi \
        -ldxguid \
        -lharfbuzz \
        -lfreetype \
        -lbz2 \
        -lpng16 \
        # -lharfbuzz_too \ # Убедитесь, что эти _too версии стандартные или ваши кастомные сборки
        # -lfreetype_too \
        -lglib-2.0 \
        -lintl \
        -liconv \
        -latomic \
        -lm \
        -lpcre2-8 \
        -lshlwapi \
        -lcomdlg32 \
        -loleaut32 \
        -limm32 \
        -ldnsapi \
        -liphlpapi \
        -lgdi32 \
        -lcrypt32 \
        -lmpr \
        -luserenv \
        -lversion \
        -lz \
        -lpcre2-16 \
        -lnetapi32 \
        -lws2_32 \
        -ladvapi32 \
        -lkernel32 \
        -lole32 \
        -lshell32 \
        -luuid \
        -luser32 \
        -lwinmm
        # -lmingw32 # qmake обычно добавляет это в конце, если необходимо
# Флаги компоновщика для статической линковки
QMAKE_LFLAGS += -static
QMAKE_LFLAGS += -static-libgcc
QMAKE_LFLAGS += -static-libstdc++
SOURCES += \
    main.cpp \
    # ... (другие ваши .cpp файлы) ...
#HEADERS += \
    # ... (другие ваши .h файлы) ...
