cmake_minimum_required(VERSION 3.16)

find_package(Qt6 REQUIRED COMPONENTS Core Sql SqlPrivate)

# ── Find OpenSSL (required by SQLCipher v4.6.1) ───────────────────────────
find_package(OpenSSL REQUIRED)

# ── Platform feature detection ────────────────────────────────────────────
include(CheckFunctionExists)
include(CheckIncludeFile)
check_function_exists(fdatasync            HAVE_FDATASYNC)
check_function_exists(gmtime_r             HAVE_GMTIME_R)
check_function_exists(isnan                HAVE_ISNAN)
check_function_exists(localtime_r          HAVE_LOCALTIME_R)
check_function_exists(localtime_s          HAVE_LOCALTIME_S)
check_function_exists(malloc_usable_size   HAVE_MALLOC_USABLE_SIZE)
check_function_exists(pread                HAVE_PREAD)
check_function_exists(pread64              HAVE_PREAD64)
check_function_exists(pwrite               HAVE_PWRITE)
check_function_exists(pwrite64             HAVE_PWRITE64)
check_function_exists(usleep               HAVE_USLEEP)
check_function_exists(utime                HAVE_UTIME)
check_include_file(inttypes.h              HAVE_INTTYPES_H)
check_include_file(malloc.h                HAVE_MALLOC_H)
check_include_file(stdint.h                HAVE_STDINT_H)

set(SQLCIPHER_COMPILE_DEFS
    SQLCIPHER_CRYPTO_OPENSSL
    SQLITE_HAS_CODEC
    SQLITE_TEMP_STORE=2
    SQLITE_ENABLE_COLUMN_METADATA
    SQLITE_ENABLE_FTS3
    SQLITE_ENABLE_FTS3_PARENTHESIS
    SQLITE_ENABLE_FTS4
    SQLITE_ENABLE_FTS5
    SQLITE_ENABLE_JSON1
    SQLITE_ENABLE_MATH_FUNCTIONS
    SQLITE_ENABLE_RTREE
    SQLITE_OMIT_COMPLETE
    $<$<BOOL:${HAVE_FDATASYNC}>:HAVE_FDATASYNC>
    $<$<BOOL:${HAVE_GMTIME_R}>:HAVE_GMTIME_R>
    $<$<BOOL:${HAVE_ISNAN}>:HAVE_ISNAN>
    $<$<BOOL:${HAVE_LOCALTIME_R}>:HAVE_LOCALTIME_R>
    $<$<BOOL:${HAVE_LOCALTIME_S}>:HAVE_LOCALTIME_S>
    $<$<BOOL:${HAVE_MALLOC_USABLE_SIZE}>:HAVE_MALLOC_USABLE_SIZE>
    $<$<BOOL:${HAVE_PREAD}>:HAVE_PREAD>
    $<$<BOOL:${HAVE_PREAD64}>:HAVE_PREAD64>
    $<$<BOOL:${HAVE_PWRITE}>:HAVE_PWRITE>
    $<$<BOOL:${HAVE_PWRITE64}>:HAVE_PWRITE64>
    $<$<BOOL:${HAVE_USLEEP}>:HAVE_USLEEP>
    $<$<BOOL:${HAVE_UTIME}>:HAVE_UTIME>
    $<$<BOOL:${HAVE_INTTYPES_H}>:HAVE_INTTYPES_H>
    $<$<BOOL:${HAVE_MALLOC_H}>:HAVE_MALLOC_H>
    $<$<BOOL:${HAVE_STDINT_H}>:HAVE_STDINT_H>
    $<$<CONFIG:Release>:NDEBUG>
)

# ── Build as a plain STATIC LIBRARY (not qt_add_plugin) ──────────────────
# qt_add_plugin generates _init.cpp that calls qt_static_plugin_*() which
# requires a specific MOC pass that is unreliable in subdirectory builds.
# A plain static library avoids this entirely — we register the plugin
# manually in main.cpp using Q_IMPORT_PLUGIN after providing the
# qt_static_plugin function ourselves via the MOC output in smain.h/cpp.
set(CMAKE_AUTOMOC ON)

add_library(QSqlCipherDriverPlugin STATIC
    smain.h
    smain.cpp
    qsql_sqlite.cpp
    qsql_sqlite_p.h
    qsql_sqlite_vfs.cpp
    qsql_sqlite_vfs_p.h
    ../sqlcipher/sqlite3.c
    ../sqlcipher/sqlite3.h
)

target_compile_definitions(QSqlCipherDriverPlugin
    PRIVATE
        ${SQLCIPHER_COMPILE_DEFS}
        QT_PLUGIN
        QT_STATICPLUGIN
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)

target_include_directories(QSqlCipherDriverPlugin
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../sqlcipher
        ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(QSqlCipherDriverPlugin
    PRIVATE
        Qt6::Core
        Qt6::Sql
        Qt6::SqlPrivate
        OpenSSL::Crypto
)

if(WIN32)
    target_link_libraries(QSqlCipherDriverPlugin
        PRIVATE ws2_32 wsock32 crypt32
    )
endif()
