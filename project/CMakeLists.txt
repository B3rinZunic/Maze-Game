cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(Project_6
        VERSION 1.0
        DESCRIPTION "Maze Game"
        LANGUAGES C)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/services/Display_service
                                        ${CMAKE_CURRENT_LIST_DIR}/services/Sensor_service
                                        ${CMAKE_CURRENT_LIST_DIR}/general
                                        ${CMAKE_CURRENT_LIST_DIR}/services/ServoMotor_service
                                        ${CMAKE_CURRENT_LIST_DIR}/services/Game_service
                                        ${CMAKE_CURRENT_LIST_DIR}/services/Score_service
                                        ${CMAKE_CURRENT_LIST_DIR}/services/Goal_service
)
target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src/main.c
                           ${CMAKE_CURRENT_LIST_DIR}/services/Display_service/display_ssd1306.c
                           ${CMAKE_CURRENT_LIST_DIR}/services/Sensor_service/sensor_mpu6050.c
                           ${CMAKE_CURRENT_LIST_DIR}/services/ServoMotor_service/servomotor.c
                           ${CMAKE_CURRENT_LIST_DIR}/services/Game_service/game.c
                           ${CMAKE_CURRENT_LIST_DIR}/services/Score_service/score.c
                           ${CMAKE_CURRENT_LIST_DIR}/services/Goal_service/goal.c
)


