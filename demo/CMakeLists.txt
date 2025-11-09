cmake_minimum_required(VERSION 3.16)
project(HeatSimExecutor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Единственный .cpp (если их больше — добавь сюда)
add_executable(${PROJECT_NAME}
    main.cpp
)

# include-пути
target_include_directories(${PROJECT_NAME} BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/SIM
    ${CMAKE_CURRENT_SOURCE_DIR}/Executor
    ${CMAKE_CURRENT_SOURCE_DIR}/Scheduler    # <-- добавь папку планировщика
)

# если у Scheduler есть .cpp — добавь в target или собери отдельной библиотекой
# add_library(scheduler STATIC Scheduler/scheduler.cpp ...)
# target_link_libraries(${PROJECT_NAME} PRIVATE scheduler)

# pthread уже линкуется через Threads::Threads
# target_compile_definitions(${PROJECT_NAME} PRIVATE SIM_TRUECOLOR=1)

target_compile_definitions(${PROJECT_NAME} PRIVATE SIM_TRUECOLOR=1)


find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)

# Предупреждения (по желанию)
if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive-)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Режим сборки по умолчанию
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
