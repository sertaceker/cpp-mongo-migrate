cmake_minimum_required(VERSION 4.1)
project(mongo-migrate)

set(CMAKE_CXX_STANDARD 20)

find_package(bsoncxx CONFIG REQUIRED)
find_package(mongocxx CONFIG REQUIRED)

add_executable(${PROJECT_NAME}
        main.cpp
        migration.hpp)


target_link_libraries(${PROJECT_NAME} PRIVATE
        mongo::bsoncxx_static
        mongo::mongocxx_static)

file(GLOB_RECURSE MIGRATION_FILES "migrations/*.cpp")

target_sources(${PROJECT_NAME} PRIVATE
        ${MIGRATION_FILES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
        migrations
)